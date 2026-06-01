//*****************************************************************************
//
// main.c
//
// CC3200 + BMA222 + SSD1351 OLED
//
// Modes:
//   Button 1 / SW2 / GPIO22 / PIN_15:
//      STEP MODE
//      If already in STEP MODE, pressing again resets step count to zero.
//
//   Button 2 / SW3 / GPIO13 / PIN_04:
//      PUSHUP MODE
//      If already in PUSHUP MODE, pressing again resets pushup count to zero.
//      Pushup logic is empty for now.
//
// Step logic:
//   Standing  -> Z_deg near 90 degrees
//   Stepping  -> Z_deg drops toward 40 degrees
//   Step done -> Z_deg returns near 90 degrees
//
//   If Z_deg_filtered < 70 degrees -> step started
//   If step started and Z_deg_filtered > 72 degrees -> count 1 step
//
//*****************************************************************************

// Standard includes
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// SimpleLink / AWS networking includes
#include "simplelink.h"
#include "gpio_if.h"
#include "common.h"
#include "utils/network_utils.h"

// Driverlib includes
#include "hw_types.h"
#include "hw_ints.h"
#include "hw_memmap.h"
#include "hw_common_reg.h"
#include "rom.h"
#include "rom_map.h"
#include "interrupt.h"
#include "prcm.h"
#include "utils.h"
#include "uart.h"
#include "gpio.h"
#include "pin.h"

// Common interface includes
#include "uart_if.h"
#include "i2c_if.h"

#include "pinmux.h"
#include "oled_ssd1351.h"

//*****************************************************************************
// MACRO DEFINITIONS
//*****************************************************************************

#define APPLICATION_VERSION              "1.5.0"
#define APP_NAME                         "BMA222 Multi Mode Counter"

#define UART_PRINT                       Report
#define SUCCESS                          0
#define FAILURE                          -1

//*****************************************************************************
// AWS IOT DEFINITIONS
//*****************************************************************************

#define DATE                30
#define MONTH               5
#define YEAR                2026
#define HOUR                13
#define MINUTE              0
#define SECOND              0

#define SERVER_NAME         "a1a7laiiez8sbe-ats.iot.us-east-2.amazonaws.com"
#define AWS_DST_PORT        8443

#define POSTHEADER          "POST /things/Ohio_Thing/shadow HTTP/1.1\r\n"
#define HOSTHEADER          "Host: a1a7laiiez8sbe-ats.iot.us-east-2.amazonaws.com\r\n"
#define CHEADER             "Connection: Keep-Alive\r\n"
#define CTHEADER            "Content-Type: application/json; charset=utf-8\r\n"
#define CLHEADER1           "Content-Length: "
#define CLHEADER2           "\r\n\r\n"


//*****************************************************************************
// BMA222 DEFINITIONS
//*****************************************************************************

#define BMA222_ADDR                      0x18
#define BMA222_CHIP_ID                   0x00
#define BMA222_DATA_START                0x02

//*****************************************************************************
// SAMPLING DEFINITIONS
//*****************************************************************************

#define SAMPLE_PERIOD_MS                 12.5f

/*
 * 12.5 ms sampling = 80 samples/second.
 * delay_count = 80,000,000 * 0.0125 / 3 = 333,333
 */
#define SAMPLE_DELAY_CYCLES              333333

/*
 * 40 samples * 12.5 ms = 500 ms terminal print interval.
 */
#define TERMINAL_PRINT_INTERVAL_SAMPLES  40

#define RAD_TO_DEG                       57.2957795f

//*****************************************************************************
// Z-ANGLE STEP COUNTER DEFINITIONS
//*****************************************************************************

#define Z_STEP_START_DEG                 70.0f
#define Z_STEP_RETURN_DEG                72.0f

/*
 * 8 samples * 12.5 ms = 100 ms minimum between counted steps.
 * 64 samples * 12.5 ms = 800 ms timeout.
 */
#define STEP_MIN_INTERVAL_SAMPLES        8
#define STEP_MAX_ACTIVE_SAMPLES          64

//*****************************************************************************
// BUTTON DEFINITIONS
//*****************************************************************************

/*
 * CC3200 LaunchPad onboard buttons:
 *
 * SW2 = GPIO22 = PIN_15
 * SW3 = GPIO13 = PIN_04
 *
 * Buttons are active HIGH:
 *   not pressed -> 0
 *   pressed     -> 1
 */
#define BUTTON_STEP_PIN                  PIN_15
#define BUTTON_STEP_PORT                 GPIOA2_BASE
#define BUTTON_STEP_GPIO                 0x40

#define BUTTON_PUSHUP_PIN                PIN_04
#define BUTTON_PUSHUP_PORT               GPIOA1_BASE
#define BUTTON_PUSHUP_GPIO               0x20

/*
 * 16 samples * 12.5 ms = 200 ms debounce/cooldown.
 */
#define BUTTON_COOLDOWN_SAMPLES          16

#define IR_SENSOR_PIN                   PIN_18
#define IR_SENSOR_PORT                  GPIOA3_BASE
#define IR_SENSOR_GPIO                  0x10
#define IR_SENSOR_PRCM                  PRCM_GPIOA3

#define IR_SENSOR_ACTIVE_LOW            1

#define PUSHUP_MIN_INTERVAL_SAMPLES     24

//*****************************************************************************
// GLOBAL VARIABLES
//*****************************************************************************

static int set_time(void);
static int AWS_Init(void);
static int AWS_PostCounter(const char *mode_name, unsigned long count_value);

static int g_tls_sock_id = -1;
static int g_aws_ready = 0;


#if defined(ccs)
extern void (* const g_pfnVectors[])(void);
#endif

#if defined(ewarm)
extern uVectorEntry __vector_table;
#endif

typedef enum
{
    APP_MODE_STEP = 0,
    APP_MODE_PUSHUP = 1
} AppMode_t;

static AppMode_t current_mode = APP_MODE_STEP;

// Step counter variables
static float z_deg_filtered = 90.0f;

static unsigned long step_count = 0;
static unsigned long sample_count = 0;
static unsigned long last_step_sample = 0;

static int step_active = 0;
static unsigned long active_start_sample = 0;

// Pushup placeholder variables
static unsigned long pushup_count = 0;
static unsigned long pushup_sample_count = 0;
static unsigned long last_pushup_sample = 0;

static unsigned char floor_near_previous = 0;

// Button edge detection
static unsigned char button_step_previous = 0;
static unsigned char button_pushup_previous = 0;

static unsigned int button_step_cooldown = 0;
static unsigned int button_pushup_cooldown = 0;

//*****************************************************************************
// BOARD INITIALIZATION
//*****************************************************************************

static void BoardInit(void)
{
#ifndef USE_TIRTOS
#if defined(ccs)
    MAP_IntVTableBaseSet((unsigned long)&g_pfnVectors[0]);
#endif

#if defined(ewarm)
    MAP_IntVTableBaseSet((unsigned long)&__vector_table);
#endif
#endif

    MAP_IntMasterEnable();
    MAP_IntEnable(FAULT_SYSTICK);

    PRCMCC3200MCUInit();
}

//*****************************************************************************
// UART BANNER
//*****************************************************************************

static void DisplayBanner(char *AppName)
{
    Report("\n\n\n\r");
    Report("\t\t *************************************************\n\r");
    Report("\t\t      CC3200 %s Application\n\r", AppName);
    Report("\t\t *************************************************\n\r");
    Report("\n\n\n\r");
}

//*****************************************************************************
// HELPER FUNCTIONS
//*****************************************************************************

static float clamp_float(float value, float min_value, float max_value)
{
    if(value < min_value)
    {
        return min_value;
    }

    if(value > max_value)
    {
        return max_value;
    }

    return value;
}

static float CalculateAxisAngleDeg(float axis_value, float x, float y, float z)
{
    float mag;
    float ratio;
    float angle_deg;

    mag = sqrtf((x * x) + (y * y) + (z * z));

    if(mag < 0.01f)
    {
        return 0.0f;
    }

    ratio = axis_value / mag;

    ratio = clamp_float(ratio, -1.0f, 1.0f);

    angle_deg = acosf(ratio) * RAD_TO_DEG;

    return angle_deg;
}

//*****************************************************************************
// BUTTON FUNCTIONS
//*****************************************************************************

static void Buttons_Init(void)
{
    MAP_PRCMPeripheralClkEnable(PRCM_GPIOA1, PRCM_RUN_MODE_CLK);
    MAP_PRCMPeripheralClkEnable(PRCM_GPIOA2, PRCM_RUN_MODE_CLK);

    MAP_PinTypeGPIO(BUTTON_STEP_PIN, PIN_MODE_0, 0);
    MAP_PinTypeGPIO(BUTTON_PUSHUP_PIN, PIN_MODE_0, 0);

    MAP_GPIODirModeSet(BUTTON_STEP_PORT,
                       BUTTON_STEP_GPIO,
                       GPIO_DIR_MODE_IN);

    MAP_GPIODirModeSet(BUTTON_PUSHUP_PORT,
                       BUTTON_PUSHUP_GPIO,
                       GPIO_DIR_MODE_IN);
}

static unsigned char ButtonStep_IsPressed(void)
{
    if(MAP_GPIOPinRead(BUTTON_STEP_PORT, BUTTON_STEP_GPIO) & BUTTON_STEP_GPIO)
    {
        return 1;
    }

    return 0;
}

static unsigned char ButtonPushup_IsPressed(void)
{
    if(MAP_GPIOPinRead(BUTTON_PUSHUP_PORT, BUTTON_PUSHUP_GPIO) & BUTTON_PUSHUP_GPIO)
    {
        return 1;
    }

    return 0;
}

static unsigned char ButtonStep_PressedEvent(void)
{
    unsigned char current_state;
    unsigned char event;

    current_state = ButtonStep_IsPressed();
    event = 0;

    if(button_step_cooldown > 0)
    {
        button_step_cooldown--;
    }
    else
    {
        if((current_state == 1) && (button_step_previous == 0))
        {
            event = 1;
            button_step_cooldown = BUTTON_COOLDOWN_SAMPLES;
        }
    }

    button_step_previous = current_state;

    return event;
}

static unsigned char ButtonPushup_PressedEvent(void)
{
    unsigned char current_state;
    unsigned char event;

    current_state = ButtonPushup_IsPressed();
    event = 0;

    if(button_pushup_cooldown > 0)
    {
        button_pushup_cooldown--;
    }
    else
    {
        if((current_state == 1) && (button_pushup_previous == 0))
        {
            event = 1;
            button_pushup_cooldown = BUTTON_COOLDOWN_SAMPLES;
        }
    }

    button_pushup_previous = current_state;

    return event;
}

//*****************************************************************************
// IR SENSOR FUNCTIONS
//*****************************************************************************

static void IRSensor_Init(void)
{
    MAP_PRCMPeripheralClkEnable(IR_SENSOR_PRCM, PRCM_RUN_MODE_CLK);

    MAP_PinTypeGPIO(IR_SENSOR_PIN, PIN_MODE_0, 0);

    MAP_GPIODirModeSet(IR_SENSOR_PORT,
                       IR_SENSOR_GPIO,
                       GPIO_DIR_MODE_IN);
}

static unsigned char IRSensor_IsFloorNear(void)
{
    unsigned long pin_value;

    pin_value = MAP_GPIOPinRead(IR_SENSOR_PORT, IR_SENSOR_GPIO);

#if IR_SENSOR_ACTIVE_LOW
    if((pin_value & IR_SENSOR_GPIO) == 0)
    {
        return 1;
    }
    else
    {
        return 0;
    }
#else
    if(pin_value & IR_SENSOR_GPIO)
    {
        return 1;
    }
    else
    {
        return 0;
    }
#endif
}

//*****************************************************************************
// STEP COUNTER
//*****************************************************************************

static void StepCounter_Reset(void)
{
    z_deg_filtered = 90.0f;

    step_count = 0;
    sample_count = 0;
    last_step_sample = 0;

    step_active = 0;
    active_start_sample = 0;

    OLED_ShowStepCount(step_count);

    Report("\n\rSTEP MODE selected/reset.\n\r");
    Report("Step starts when Z_deg < %.2f degrees.\n\r", Z_STEP_START_DEG);
    Report("Step counts when Z_deg returns > %.2f degrees.\n\r\n\r", Z_STEP_RETURN_DEG);
}

static void StepCounter_UpdateFromZAngle(float z_deg)
{
    sample_count++;

    /*
     * Fast filter for running.
     * 0.25 = old value weight
     * 0.75 = new value weight
     */
    z_deg_filtered = (0.25f * z_deg_filtered) + (0.75f * z_deg);

    /*
     * State 0:
     * Wait for Z angle to drop below start threshold.
     */
    if(step_active == 0)
    {
        if((z_deg_filtered < Z_STEP_START_DEG) &&
           ((sample_count - last_step_sample) >= STEP_MIN_INTERVAL_SAMPLES))
        {
            step_active = 1;
            active_start_sample = sample_count;
        }
    }

    /*
     * State 1:
     * Z angle dropped. Count step when it returns upward.
     */
    else
    {
        if(z_deg_filtered > Z_STEP_RETURN_DEG)
        {
            step_count++;
            last_step_sample = sample_count;
            step_active = 0;

            Report("STEP COUNT = %lu | Z_deg_filtered = %.2f\n\r",
                   step_count,
                   z_deg_filtered);

            OLED_ShowStepCount(step_count);
        }

        if((sample_count - active_start_sample) > STEP_MAX_ACTIVE_SAMPLES)
        {
            step_active = 0;
        }
    }
}

//*****************************************************************************
// PUSHUP COUNTER
//*****************************************************************************

static void PushupCounter_Reset(void)
{
    pushup_count = 0;
    pushup_sample_count = 0;
    last_pushup_sample = 0;
    floor_near_previous = 0;

    OLED_ShowError("PUSHUP", "0");

    Report("\n\rPUSHUP MODE selected/reset.\n\r");
    Report("Pushup count increments when IR sensor detects floor close.\n\r\n\r");
}

static void PushupCounter_Update(float x_g,
                                 float y_g,
                                 float z_g,
                                 float x_deg,
                                 float y_deg,
                                 float z_deg)
{
    unsigned char floor_near;
    char buffer[16];

    /*
     * Accelerometer values are unused for now.
     * Later, we can combine accelerometer + IR for better pushup detection.
     */
    (void)x_g;
    (void)y_g;
    (void)z_g;
    (void)x_deg;
    (void)y_deg;
    (void)z_deg;

    pushup_sample_count++;

    floor_near = IRSensor_IsFloorNear();

    /*
     * Count only on transition:
     *
     *   floor far  -> floor near
     *
     * This prevents multiple counts while you stay close to the floor.
     */
    if((floor_near == 1) &&
       (floor_near_previous == 0) &&
       ((pushup_sample_count - last_pushup_sample) >= PUSHUP_MIN_INTERVAL_SAMPLES))
    {
        pushup_count++;
        last_pushup_sample = pushup_sample_count;

        sprintf(buffer, "%lu", pushup_count);

        Report("PUSHUP COUNT = %lu\n\r", pushup_count);

        OLED_ShowError("PUSHUP", buffer);
    }

    floor_near_previous = floor_near;
}

//*****************************************************************************
// MODE HANDLER
//*****************************************************************************

static void HandleModeButtons(void)
{
    int upload_result;

    /*
     * Button 1 = STEP MODE button.
     */
    if(ButtonStep_PressedEvent())
    {
        if(current_mode == APP_MODE_STEP)
        {
            /*
             * Already in STEP MODE:
             * Save/upload current step count, then reset.
             */
            UART_PRINT("\n\rSTEP button pressed again.\n\r");
            UART_PRINT("Saving step count = %lu to AWS...\n\r", step_count);

            OLED_ShowError("SAVING", "STEP");

            upload_result = AWS_PostCounter("step", step_count);

            if(upload_result == SUCCESS)
            {
                UART_PRINT("Step count uploaded successfully.\n\r");
                OLED_ShowError("STEP", "SAVED");
            }
            else
            {
                UART_PRINT("Step upload failed. Error = %d\n\r", upload_result);
                OLED_ShowError("STEP", "FAIL");
            }

            MAP_UtilsDelay(2000000);

            StepCounter_Reset();
        }
        else
        {
            /*
             * Not in STEP MODE:
             * Switch to STEP MODE and reset step counter.
             */
            current_mode = APP_MODE_STEP;
            StepCounter_Reset();
        }
    }

    /*
     * Button 2 = PUSHUP MODE button.
     */
    if(ButtonPushup_PressedEvent())
    {
        if(current_mode == APP_MODE_PUSHUP)
        {
            /*
             * Already in PUSHUP MODE:
             * Save/upload current pushup count, then reset.
             */
            UART_PRINT("\n\rPUSHUP button pressed again.\n\r");
            UART_PRINT("Saving pushup count = %lu to AWS...\n\r", pushup_count);

            OLED_ShowError("SAVING", "PUSHUP");

            upload_result = AWS_PostCounter("pushup", pushup_count);

            if(upload_result == SUCCESS)
            {
                UART_PRINT("Pushup count uploaded successfully.\n\r");
                OLED_ShowError("PUSHUP", "SAVED");
            }
            else
            {
                UART_PRINT("Pushup upload failed. Error = %d\n\r", upload_result);
                OLED_ShowError("PUSHUP", "FAIL");
            }

            MAP_UtilsDelay(2000000);

            PushupCounter_Reset();
        }
        else
        {
            /*
             * Not in PUSHUP MODE:
             * Switch to PUSHUP MODE and reset pushup counter.
             */
            current_mode = APP_MODE_PUSHUP;
            PushupCounter_Reset();
        }
    }
}
//*****************************************************************************
// BMA222 READ + MODE LOOP
//*****************************************************************************

static void PrintBMA222Data(void)
{
    int iRetVal;
    unsigned char reg;
    unsigned char chipId;
    unsigned char data[6];

    signed char x_raw;
    signed char y_raw;
    signed char z_raw;

    float x_g;
    float y_g;
    float z_g;

    float x_deg;
    float y_deg;
    float z_deg;

    unsigned long print_counter = 0;

    /*
     * Check BMA222 chip ID.
     */
    reg = BMA222_CHIP_ID;
    iRetVal = I2C_IF_ReadFrom(BMA222_ADDR, &reg, 1, &chipId, 1);

    if(iRetVal < 0)
    {
        Report("BMA222 not detected.\n\r");
        Report("Check J2/J3 jumpers and I2C address 0x18.\n\r");

        OLED_ShowError("BMA222", "NOT FOUND");

        return;
    }

    Report("BMA222 detected. CHIP ID = 0x%02x\n\r", chipId);

    current_mode = APP_MODE_STEP;
    StepCounter_Reset();

    while(1)
    {
        /*
         * Check buttons every loop.
         */
        HandleModeButtons();

        reg = BMA222_DATA_START;

        /*
         * Read 6 bytes starting from 0x02:
         *   data[0] = X LSB/status
         *   data[1] = X acceleration MSB
         *   data[2] = Y LSB/status
         *   data[3] = Y acceleration MSB
         *   data[4] = Z LSB/status
         *   data[5] = Z acceleration MSB
         */
        iRetVal = I2C_IF_ReadFrom(BMA222_ADDR, &reg, 1, data, 6);

        if(iRetVal == SUCCESS)
        {
            x_raw = (signed char)data[1];
            y_raw = (signed char)data[3];
            z_raw = (signed char)data[5];

            /*
             * Default BMA222 range after reset is +/-2g.
             * Sensitivity at +/-2g = 64 LSB/g.
             */
            x_g = ((float)x_raw) / 64.0f;
            y_g = ((float)y_raw) / 64.0f;
            z_g = ((float)z_raw) / 64.0f;

            /*
             * Axis angle relative to gravity vector.
             */
            x_deg = CalculateAxisAngleDeg(x_g, x_g, y_g, z_g);
            y_deg = CalculateAxisAngleDeg(y_g, x_g, y_g, z_g);
            z_deg = CalculateAxisAngleDeg(z_g, x_g, y_g, z_g);

            if(current_mode == APP_MODE_STEP)
            {
                StepCounter_UpdateFromZAngle(z_deg);
            }
            else if(current_mode == APP_MODE_PUSHUP)
            {
                PushupCounter_Update(x_g, y_g, z_g, x_deg, y_deg, z_deg);
            }

            /*
             * Print to terminal every 0.5 seconds.
             */
            print_counter++;

            if(print_counter >= TERMINAL_PRINT_INTERVAL_SAMPLES)
            {
                print_counter = 0;

                if(current_mode == APP_MODE_STEP)
                {
                    Report("MODE = STEP | X_deg = %.2f, Y_deg = %.2f, Z_deg = %.2f | Z_filt = %.2f | Active = %d | Steps = %lu\n\r",
                           x_deg,
                           y_deg,
                           z_deg,
                           z_deg_filtered,
                           step_active,
                           step_count);
                }
                else
                {
                    Report("MODE = PUSHUP | X_deg = %.2f, Y_deg = %.2f, Z_deg = %.2f | Pushups = %lu\n\r",
                           x_deg,
                           y_deg,
                           z_deg,
                           pushup_count);
                }
            }
        }
        else
        {
            Report("I2C read failed.\n\r");
        }

        MAP_UtilsDelay(SAMPLE_DELAY_CYCLES);
    }
}


//*****************************************************************************
// AWS FUNCTIONS
//*****************************************************************************

static int set_time(void)
{
    long retVal;

    g_time.tm_day = DATE;
    g_time.tm_mon = MONTH;
    g_time.tm_year = YEAR;
    g_time.tm_sec = SECOND;
    g_time.tm_hour = HOUR;
    g_time.tm_min = MINUTE;

    retVal = sl_DevSet(SL_DEVICE_GENERAL_CONFIGURATION,
                       SL_DEVICE_GENERAL_CONFIGURATION_DATE_TIME,
                       sizeof(SlDateTime),
                       (unsigned char *)(&g_time));

    ASSERT_ON_ERROR(retVal);

    return SUCCESS;
}

static int AWS_Init(void)
{
    long lRetVal;

    UART_PRINT("\n\rInitializing AWS connection...\n\r");

    g_app_config.host = SERVER_NAME;
    g_app_config.port = AWS_DST_PORT;

    lRetVal = connectToAccessPoint();
    UART_PRINT("WiFi connect returned: %d\n\r", lRetVal);

    if(lRetVal < 0)
    {
        UART_PRINT("WiFi connection failed.\n\r");
        g_aws_ready = 0;
        return lRetVal;
    }

    lRetVal = set_time();
    UART_PRINT("Set time returned: %d\n\r", lRetVal);

    if(lRetVal < 0)
    {
        UART_PRINT("Unable to set time in the device.\n\r");
        g_aws_ready = 0;
        return lRetVal;
    }

    g_tls_sock_id = tls_connect();
    UART_PRINT("TLS connect returned: %d\n\r", g_tls_sock_id);

    if(g_tls_sock_id < 0)
    {
        UART_PRINT("TLS connection failed.\n\r");
        g_aws_ready = 0;
        return g_tls_sock_id;
    }

    g_aws_ready = 1;

    UART_PRINT("AWS connection ready.\n\r");

    return SUCCESS;
}

static int AWS_PostCounter(const char *mode_name, unsigned long count_value)
{
    char acSendBuff[768];
    char acRecvbuff[1460];
    char cCLLength[32];
    char jsonBody[256];
    char *pcBufHeaders;
    int dataLength;
    int lRetVal;

    if(g_aws_ready == 0 || g_tls_sock_id < 0)
    {
        UART_PRINT("AWS not ready. Trying to reconnect...\n\r");

        lRetVal = AWS_Init();

        if(lRetVal < 0)
        {
            UART_PRINT("AWS reconnect failed. Upload skipped.\n\r");
            return lRetVal;
        }
    }

    /*
     * AWS IoT shadow JSON payload.
     *
     * Example:
     * {
     *   "state": {
     *     "desired": {
     *       "mode": "step",
     *       "count": 12
     *     }
     *   }
     * }
     */
    sprintf(jsonBody,
            "{"
                "\"state\":{"
                    "\"desired\":{"
                        "\"mode\":\"%s\","
                        "\"count\":%lu"
                    "}"
                "}"
            "}\r\n\r\n",
            mode_name,
            count_value);

    dataLength = strlen(jsonBody);

    pcBufHeaders = acSendBuff;

    strcpy(pcBufHeaders, POSTHEADER);
    pcBufHeaders += strlen(POSTHEADER);

    strcpy(pcBufHeaders, HOSTHEADER);
    pcBufHeaders += strlen(HOSTHEADER);

    strcpy(pcBufHeaders, CHEADER);
    pcBufHeaders += strlen(CHEADER);

    strcpy(pcBufHeaders, CTHEADER);
    pcBufHeaders += strlen(CTHEADER);

    strcpy(pcBufHeaders, CLHEADER1);
    pcBufHeaders += strlen(CLHEADER1);

    sprintf(cCLLength, "%d", dataLength);
    strcpy(pcBufHeaders, cCLLength);
    pcBufHeaders += strlen(cCLLength);

    strcpy(pcBufHeaders, CLHEADER2);
    pcBufHeaders += strlen(CLHEADER2);

    strcpy(pcBufHeaders, jsonBody);
    pcBufHeaders += strlen(jsonBody);

    UART_PRINT("\n\rSending AWS POST:\n\r");
    UART_PRINT(acSendBuff);
    UART_PRINT("\n\r");

    lRetVal = sl_Send(g_tls_sock_id, acSendBuff, strlen(acSendBuff), 0);

    if(lRetVal < 0)
    {
        UART_PRINT("POST failed. Error Number: %d\n\r", lRetVal);

        sl_Close(g_tls_sock_id);
        g_tls_sock_id = -1;
        g_aws_ready = 0;

        return lRetVal;
    }

    lRetVal = sl_Recv(g_tls_sock_id, &acRecvbuff[0], sizeof(acRecvbuff), 0);

    if(lRetVal < 0)
    {
        UART_PRINT("Receive failed. Error Number: %d\n\r", lRetVal);

        sl_Close(g_tls_sock_id);
        g_tls_sock_id = -1;
        g_aws_ready = 0;

        return lRetVal;
    }

    acRecvbuff[lRetVal] = '\0';

    UART_PRINT("AWS response:\n\r");
    UART_PRINT(acRecvbuff);
    UART_PRINT("\n\r\n\r");

    return SUCCESS;
}
//*****************************************************************************
// MAIN
//*****************************************************************************

void main()
{
    BoardInit();

    /*
     * Original TI i2c_demo pinmux.
     * This configures UART and I2C for the onboard BMA222.
     */
    PinMuxConfig();

    /*
     * OLED-specific SPI and GPIO pinmux.
     */
    OLED_PinMuxConfig();

    /*
     * Onboard button GPIO setup.
     */
    Buttons_Init();

    IRSensor_Init();

    /*
     * UART terminal.
     */
    InitTerm();
    ClearTerm();

    I2C_IF_Open(I2C_MASTER_MODE_FST);

    OLED_SPI_Init();

    DisplayBanner(APP_NAME);

    OLED_Init();

    OLED_ShowStartupScreen();
    MAP_UtilsDelay(2000000);

    /*
     * Connect to WiFi + AWS IoT once at startup.
     * If this fails, counting still works. Upload will retry when button is pressed.
     */
    AWS_Init();

    PrintBMA222Data();

    while(1)
    {
    }
}
