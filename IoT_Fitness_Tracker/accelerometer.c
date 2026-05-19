/*
 * accelerometer.c
 *
 * BMA222 3-axis accelerometer driver for CC3200 (CC3200SDK_1.4.0)
 * Communicates via I2C (I2CA0_BASE)
 *
 * Used in Running Mode for step detection via peak detection on Z-axis.
 */

#include "accelerometer.h"
/* hw_types.h must come first — defines tBoolean used by driverlib headers */
#include "hw_types.h"
#include "hw_memmap.h"
#include "i2c_if.h"
#include "utils_if.h"
#include "rom.h"
#include "rom_map.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* BMA222 register map */
#define BMA222_REG_CHIP_ID      0x00
#define BMA222_REG_ACCD_X       0x02
#define BMA222_REG_ACCD_Y       0x04
#define BMA222_REG_ACCD_Z       0x06
#define BMA222_REG_PMU_RANGE    0x0F
#define BMA222_REG_PMU_BW       0x10
#define BMA222_REG_PMU_LPW      0x11
#define BMA222_CHIP_ID_VAL      0x03

#define BMA222_RANGE_2G         0x03
#define BMA222_BW_125HZ         0x0C   /* 125 Hz ODR for step detection */

/* Step detection parameters */
#define STEP_THRESHOLD_MG       250    /* mg — min peak to count as step  */
#define STEP_MIN_INTERVAL_MS    250    /* debounce: 250 ms between steps   */
#define ACCEL_SAMPLE_MS         20     /* 50 Hz sampling                   */

/* Internal state */
static volatile uint32_t g_step_count   = 0;
static int16_t  g_prev_z               = 0;
static bool     g_rising               = false;
static uint32_t g_last_step_tick       = 0;   /* ms tick from MAP_UtilsGetTickCount */

/* -----------------------------------------------------------------------
 * Accel_Init
 *   Initialise I2C peripheral and configure BMA222
 * ----------------------------------------------------------------------- */
bool Accel_Init(void)
{
    unsigned char reg, val;
    long ret;

    /* Verify chip ID */
    reg = BMA222_REG_CHIP_ID;
    ret = I2C_IF_Write(BMA222_I2C_ADDR, &reg, 1, 0);
    if (ret != 0) return false;
    ret = I2C_IF_Read(BMA222_I2C_ADDR, &val, 1);
    if (ret != 0 || val != BMA222_CHIP_ID_VAL) return false;

    /* Set range ±2g */
    unsigned char buf[2];
    buf[0] = BMA222_REG_PMU_RANGE; buf[1] = BMA222_RANGE_2G;
    I2C_IF_Write(BMA222_I2C_ADDR, buf, 2, 1);

    /* Set bandwidth 125 Hz */
    buf[0] = BMA222_REG_PMU_BW; buf[1] = BMA222_BW_125HZ;
    I2C_IF_Write(BMA222_I2C_ADDR, buf, 2, 1);

    g_step_count   = 0;
    g_prev_z       = 0;
    g_rising       = false;
    g_last_step_tick = 0;

    return true;
}

/* -----------------------------------------------------------------------
 * Accel_ReadRaw
 *   Read raw 8-bit signed X, Y, Z from BMA222 MSB registers
 * ----------------------------------------------------------------------- */
bool Accel_ReadRaw(AccelData_t *pData)
{
    unsigned char reg = BMA222_REG_ACCD_X;
    unsigned char buf[6];
    long ret;

    ret = I2C_IF_Write(BMA222_I2C_ADDR, &reg, 1, 0);
    if (ret != 0) return false;
    ret = I2C_IF_Read(BMA222_I2C_ADDR, buf, 6);
    if (ret != 0) return false;

    /* BMA222: MSB at even offsets (0,2,4), LSB nibble unused */
    pData->x = (int8_t)buf[1];   /* 0x03 MSB */
    pData->y = (int8_t)buf[3];   /* 0x05 MSB */
    pData->z = (int8_t)buf[5];   /* 0x07 MSB */

    /* Convert raw counts to mg: ±2g range → 15.6 mg/LSB (8-bit) */
    pData->x_mg = (int16_t)pData->x * 16;
    pData->y_mg = (int16_t)pData->y * 16;
    pData->z_mg = (int16_t)pData->z * 16;

    return true;
}

/* -----------------------------------------------------------------------
 * Accel_ProcessStep
 *   Call this at ACCEL_SAMPLE_MS intervals.
 *   Uses zero-crossing peak detection on Z-axis magnitude.
 *   Returns true if a new step was detected this call.
 * ----------------------------------------------------------------------- */
bool Accel_ProcessStep(uint32_t current_tick_ms)
{
    AccelData_t data;
    if (!Accel_ReadRaw(&data)) return false;

    /* Compute vector magnitude minus gravity (~1000 mg) */
    int32_t mag_sq = (int32_t)data.x_mg * data.x_mg
                   + (int32_t)data.y_mg * data.y_mg
                   + (int32_t)data.z_mg * data.z_mg;
    /* Approximate magnitude (avoid sqrt): use Z as dominant axis when upright */
    int16_t z = data.z_mg;
    int16_t delta = z - g_prev_z;

    bool step_detected = false;

    /* Rising edge: acceleration increasing past threshold */
    if (!g_rising && delta > 0 && z > STEP_THRESHOLD_MG)
    {
        g_rising = true;
    }
    /* Falling edge after peak: count step with debounce */
    else if (g_rising && delta < 0)
    {
        g_rising = false;
        uint32_t elapsed = current_tick_ms - g_last_step_tick;
        if (elapsed >= STEP_MIN_INTERVAL_MS)
        {
            g_step_count++;
            g_last_step_tick = current_tick_ms;
            step_detected = true;
        }
    }

    g_prev_z = z;
    return step_detected;
}

/* -----------------------------------------------------------------------
 * Accel_GetStepCount / Accel_ResetStepCount
 * ----------------------------------------------------------------------- */
uint32_t Accel_GetStepCount(void)
{
    return g_step_count;
}

void Accel_ResetStepCount(void)
{
    g_step_count    = 0;
    g_prev_z        = 0;
    g_rising        = false;
    g_last_step_tick = 0;
}
