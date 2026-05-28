/*
 * pin_mux_config.c
 *
 * Smart IoT Fitness Trainer — CC3200 LaunchXL  |  CC3200SDK_1.4.0
 *
 * Extends the base Lab 4 PinMuxConfig() (UART only) to add:
 *   I2C  — accelerometer (BMA222, onboard)
 *   SPI  — OLED display  (SSD1351, Adafruit 128×128)
 *   GPIO — OLED D/C, OLED RESET, IR sensor input, SW2, SW3
 *
 * Pin summary
 * ──────────────────────────────────────────────────────────────────────
 *  Signal          Phys  Dev   Header  Mode  Notes
 *  ──────────────────────────────────────────────────────────────────
 *  UART0 TX        55    55    P2      3     Debug console (from Lab 4)
 *  UART0 RX        57    57    P2      3     Debug console (from Lab 4)
 *  I2C SCL          1     1    P1      1     Accel SCL
 *  I2C SDA          2     2    P1      1     Accel SDA
 *  SPI CLK          5     5    P1      7     OLED CLK
 *  SPI MOSI (DOUT)  7     7    P2      7     OLED data
 *  SPI MISO (DIN)   6     6    P2      7     OLED (unused, wired anyway)
 *  SPI CS           8     8    P2      7     OLED chip-select
 *  OLED D/C        61    61    P1      0     GPIO output
 *  OLED RESET      62    62    P1      0     GPIO output
 *  IR sensor       15    22    P2      0     GPIO input (active LOW)
 *  SW2 (Mode)       4     4    P1      0     GPIO input, pull-up
 *  SW3 (Start)     16    55    P2      0     GPIO input, pull-up
 * ──────────────────────────────────────────────────────────────────────
 */

#include "pin_mux_config.h"
#include "hw_types.h"
#include "hw_memmap.h"
#include "hw_gpio.h"
#include "pin.h"
#include "gpio.h"
#include "prcm.h"

void PinMuxConfig(void)
{
    /* ---------------------------------------------------------------
     * Enable peripheral clocks
     * --------------------------------------------------------------- */
    PRCMPeripheralClkEnable(PRCM_UARTA0, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GSPI,   PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_I2CA0,  PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA0, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA1, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA2, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA3, PRCM_RUN_MODE_CLK);

    /* ---------------------------------------------------------------
     * UART0 — debug console (from Lab 4 pinmux.c, unchanged)
     * --------------------------------------------------------------- */
    PinTypeUART(PIN_55, PIN_MODE_3);   /* UART0 TX */
    PinTypeUART(PIN_57, PIN_MODE_3);   /* UART0 RX */

    /* ---------------------------------------------------------------
     * I2C — BMA222 accelerometer (onboard)
     *   Pin 1 → I2C SCL  (PIN_MODE_1)
     *   Pin 2 → I2C SDA  (PIN_MODE_1)
     * --------------------------------------------------------------- */
    PinTypeI2C(PIN_01, PIN_MODE_1);
    PinTypeI2C(PIN_02, PIN_MODE_1);

    /* ---------------------------------------------------------------
     * SPI (GSPI) — SSD1351 OLED display
     *   Pin 5  → CLK   (PIN_MODE_7)
     *   Pin 7  → MOSI  (PIN_MODE_7)
     *   Pin 6  → MISO  (PIN_MODE_7)  [unused by SSD1351, configured anyway]
     *   Pin 8  → CS    (PIN_MODE_7)
     * --------------------------------------------------------------- */
    PinTypeSPI(PIN_05, PIN_MODE_7);
    PinTypeSPI(PIN_07, PIN_MODE_7);
    PinTypeSPI(PIN_06, PIN_MODE_7);
    PinTypeSPI(PIN_08, PIN_MODE_7);

    /* ---------------------------------------------------------------
     * GPIO — OLED D/C (Pin 61, GPIO 6) and RESET (Pin 62, GPIO 7)
     * --------------------------------------------------------------- */
    PinTypeGPIO(PIN_61, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA0_BASE, GPIO_PIN_6, GPIO_DIR_MODE_OUT);

    PinTypeGPIO(PIN_62, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA0_BASE, GPIO_PIN_7, GPIO_DIR_MODE_OUT);

    /* ---------------------------------------------------------------
     * GPIO — IR Proximity Sensor (Pin 15, GPIO 22) — digital input
     * --------------------------------------------------------------- */
    PinTypeGPIO(PIN_15, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA2_BASE, GPIO_PIN_6, GPIO_DIR_MODE_IN);

    /* ---------------------------------------------------------------
     * GPIO — SW2 (Pin 4, GPIO 13) — input with pull-up
     * --------------------------------------------------------------- */
    PinTypeGPIO(PIN_04, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA1_BASE, GPIO_PIN_5, GPIO_DIR_MODE_IN);
    PinConfigSet(PIN_04, PIN_STRENGTH_2MA, PIN_TYPE_STD_PU);

    /* ---------------------------------------------------------------
     * GPIO — SW3 (Pin 16, GPIO 17) — input with pull-up
     * --------------------------------------------------------------- */
    PinTypeGPIO(PIN_16, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA2_BASE, GPIO_PIN_1, GPIO_DIR_MODE_IN);
    PinConfigSet(PIN_16, PIN_STRENGTH_2MA, PIN_TYPE_STD_PU);
}
