/*
 * main.c
 *
 * Smart Interactive IoT Fitness Trainer
 * CC3200 LaunchXL — CC3200SDK_1.4.0  |  CCS 10
 *
 * Authors: Parnita Chowtoori, Selena Phu  |  EEC 172 - A03
 *
 * AWS upload pattern taken from the SSL REST API demo used in Lab 4:
 *   - BoardInit / PinMuxConfig / InitTerm follow the lab template exactly.
 *   - TLS socket connect uses tls_connect() from network_utils.
 *   - HTTP POST uses the same header / body construction as http_post().
 *   - set_time() is called before any TLS operation, same as the lab.
 *
 * State Machine:
 *   IDLE → MODE_SELECT → (PUSHUP_TRACKING | RUNNING_TRACKING)
 *        → FEEDBACK → UPLOAD → IDLE
 *
 * Timers:
 *   TimerA0: 20 ms ISR — sensor sampling (IR + Accel) + button debounce
 *
 * Buttons:
 *   SW2 (GPIO 13, Pin 4):  cycle mode (MODE_SELECT) / stop session (TRACKING)
 *   SW3 (GPIO 17, Pin 16): start (IDLE) / confirm mode (MODE_SELECT)
 */

/* -----------------------------------------------------------------------
 * Standard includes
 * ----------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * CC3200 SDK — hardware abstraction
 * ----------------------------------------------------------------------- */
#include "hw_types.h"
#include "hw_ints.h"
#include "hw_memmap.h"
#include "hw_common_reg.h"
#include "rom.h"
#include "rom_map.h"
#include "interrupt.h"
#include "prcm.h"
#include "timer.h"
#include "gpio_if.h"
#include "gpio.h"
#include "spi.h"
#include "i2c_if.h"
#include "uart_if.h"
#include "utils.h"
#include "pin.h"

/* -----------------------------------------------------------------------
 * SimpleLink / TLS — same pattern as Lab 4 SSL demo
 * ----------------------------------------------------------------------- */
#include "simplelink.h"
#include "utils/network_utils.h"   /* connectToAccessPoint, tls_connect */

/* -----------------------------------------------------------------------
 * Project modules
 * ----------------------------------------------------------------------- */
#include "pin_mux_config.h"
#include "accelerometer.h"
#include "ir_sensor.h"
#include "oled.h"
#include "wifi_upload.h"

/* -----------------------------------------------------------------------
 * AWS / TLS configuration  — CHANGE ME
 * Mirrors the defines in the Lab 4 main.c exactly.
 * ----------------------------------------------------------------------- */
#define DATE        22
#define MONTH        5
#define YEAR      2026
#define HOUR        13
#define MINUTE       0
#define SECOND       0

#define APPLICATION_NAME    "FitnessTrainer"
#define APPLICATION_VERSION "1.0"

/* AWS IoT endpoint and port (same as Lab 4) */
#define SERVER_NAME     "a1a7laiiez8sbe-ats.iot.us-east-2.amazonaws.com"  /* CHANGE ME */
#define GOOGLE_DST_PORT  8443

/* HTTP headers — CHANGE thing name to match your AWS thing */
#define POSTHEADER  "POST /things/Ohio_Thing/shadow HTTP/1.1\r\n"          /* CHANGE ME */
#define HOSTHEADER  "Host: a1a7laiiez8sbe-ats.iot.us-east-2.amazonaws.com\r\n" /* CHANGE ME */
#define CHEADER     "Connection: Keep-Alive\r\n"
#define CTHEADER    "Content-Type: application/json; charset=utf-8\r\n"
#define CLHEADER1   "Content-Length: "
#define CLHEADER2   "\r\n\r\n"

/* -----------------------------------------------------------------------
 * Application configuration
 * ----------------------------------------------------------------------- */
#define TIMER_PERIOD_US     20000UL   /* 20 ms sampling interval          */
#define SESSION_TIMEOUT_S   120       /* auto-stop after 2 min            */
#define BTN_DEBOUNCE_TICKS  5         /* × 20 ms = 100 ms debounce        */

#define SW2_BASE   GPIOA1_BASE   // GPIO13
#define SW2_PIN    GPIO_PIN_5

#define SW3_BASE   GPIOA3_BASE   // GPIO17
#define SW3_PIN    GPIO_PIN_1
/* -----------------------------------------------------------------------
 * State machine
 * ----------------------------------------------------------------------- */
typedef enum {
    STATE_IDLE        = 0,
    STATE_MODE_SELECT = 1,
    STATE_PUSHUP_TRACK= 2,
    STATE_RUNNING_TRACK=3,
    STATE_FEEDBACK    = 4,
    STATE_UPLOAD      = 5
} AppState_t;

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */
/* Vector table (required by CCS startup, same as Lab 4) */
#if defined(ccs) || defined(gcc)
extern void (* const g_pfnVectors[])(void);
#endif

static volatile AppState_t  g_state         = STATE_IDLE;
static volatile WorkoutMode_t g_mode        = MODE_PUSHUP;
static volatile uint32_t    g_tick_ms       = 0;
static volatile uint32_t    g_session_start = 0;
static volatile bool        g_sw2_event     = false;
static volatile bool        g_sw3_event     = false;
static volatile uint8_t     g_sw2_deb       = 0;
static volatile uint8_t     g_sw3_deb       = 0;

/* -----------------------------------------------------------------------
 * TimerA0 ISR — 20 ms
 * ----------------------------------------------------------------------- */
void TimerA0ISR(void)
{
    MAP_TimerIntClear(TIMERA0_BASE, TIMER_TIMA_TIMEOUT);
    g_tick_ms += 20;

    if (g_state == STATE_PUSHUP_TRACK)
        IR_ProcessRep();
    else if (g_state == STATE_RUNNING_TRACK)
        Accel_ProcessStep(g_tick_ms);

    /* SW2 debounce (active LOW) */
    if (!(MAP_GPIOPinRead(SW2_BASE, SW2_PIN)))
    {
        if (g_sw2_deb < BTN_DEBOUNCE_TICKS) g_sw2_deb++;
        else { g_sw2_event = true; g_sw2_deb = 0; }
    }
    else g_sw2_deb = 0;

    /* SW3 debounce (active LOW) */
    if (!(MAP_GPIOPinRead(SW3_BASE, SW3_PIN)))
    {
        if (g_sw3_deb < BTN_DEBOUNCE_TICKS) g_sw3_deb++;
        else { g_sw3_event = true; g_sw3_deb = 0; }
    }
    else g_sw3_deb = 0;
}

/* -----------------------------------------------------------------------
 * Timer setup
 * ----------------------------------------------------------------------- */
static void Timer_Init(void)
{
    MAP_PRCMPeripheralClkEnable(PRCM_TIMERA0, PRCM_RUN_MODE_CLK);
    MAP_TimerConfigure(TIMERA0_BASE, TIMER_CFG_PERIODIC);
    uint32_t load = MAP_PRCMPeripheralClockGet(PRCM_TIMERA0)
                    / (1000000UL / TIMER_PERIOD_US);
    MAP_TimerLoadSet(TIMERA0_BASE, TIMER_A, load);
    MAP_TimerIntEnable(TIMERA0_BASE, TIMER_TIMA_TIMEOUT);
    MAP_IntEnable(INT_TIMERA0A);
    MAP_IntPrioritySet(INT_TIMERA0A, INT_PRIORITY_LVL_1);
    MAP_TimerEnable(TIMERA0_BASE, TIMER_A);
}

/* -----------------------------------------------------------------------
 * Board init — identical to Lab 4 BoardInit()
 * ----------------------------------------------------------------------- */
static void BoardInit(void)
{
#ifndef USE_TIRTOS
#if defined(ccs)
    MAP_IntVTableBaseSet((unsigned long)&g_pfnVectors[0]);
#endif
#endif
    MAP_IntMasterEnable();
    MAP_IntEnable(FAULT_SYSTICK);
    PRCMCC3200MCUInit();
}

static void GPIO_Init(void)
{
    MAP_PRCMPeripheralClkEnable(PRCM_GPIOA1, PRCM_RUN_MODE_CLK);
    MAP_PRCMPeripheralClkEnable(PRCM_GPIOA3, PRCM_RUN_MODE_CLK);

    MAP_GPIODirModeSet(GPIOA1_BASE, GPIO_PIN_5, GPIO_DIR_MODE_IN);
    MAP_GPIODirModeSet(GPIOA3_BASE, GPIO_PIN_1, GPIO_DIR_MODE_IN);
}

/* -----------------------------------------------------------------------
 * set_time — identical to Lab 4, required before any TLS connect
 * ----------------------------------------------------------------------- */
static int set_time(void)
{
    long retVal;
    g_time.tm_day  = DATE;
    g_time.tm_mon  = MONTH;
    g_time.tm_year = YEAR;
    g_time.tm_sec  = SECOND;
    g_time.tm_hour = HOUR;
    g_time.tm_min  = MINUTE;
    retVal = sl_DevSet(SL_DEVICE_GENERAL_CONFIGURATION,
                       SL_DEVICE_GENERAL_CONFIGURATION_DATE_TIME,
                       sizeof(SlDateTime), (unsigned char *)(&g_time));
    ASSERT_ON_ERROR(retVal);
    return SUCCESS;
}

/* -----------------------------------------------------------------------
 * http_post_workout
 *   Builds and sends a JSON body describing the completed workout session.
 *   Uses the same send/recv pattern as Lab 4 http_post().
 *
 *   JSON bodies:
 *     Push-up:  {"state":{"desired":{"mode":"pushup","reps":N,"time_s":T}}}
 *     Running:  {"state":{"desired":{"mode":"running","steps":N,"time_s":T}}}
 * ----------------------------------------------------------------------- */
static int http_post_workout(int iTLSSockID,
                             WorkoutMode_t mode,
                             uint32_t count,
                             uint32_t duration_s)
{
    char acSendBuff[512];
    char acRecvbuff[1460];
    char cCLLength[16];
    char *pcBufHeaders;
    char body[128];
    int  lRetVal = 0;

    /* Build JSON body */
    if (mode == MODE_PUSHUP)
        snprintf(body, sizeof(body),
            "{\"state\":{\"desired\":{\"mode\":\"pushup\","
            "\"reps\":%lu,\"time_s\":%lu}}}",
            (unsigned long)count, (unsigned long)duration_s);
    else
        snprintf(body, sizeof(body),
            "{\"state\":{\"desired\":{\"mode\":\"running\","
            "\"steps\":%lu,\"time_s\":%lu}}}",
            (unsigned long)count, (unsigned long)duration_s);

    int bodyLen = strlen(body);

    /* Build HTTP request — same order as Lab 4 http_post() */
    pcBufHeaders = acSendBuff;
    strcpy(pcBufHeaders, POSTHEADER);  pcBufHeaders += strlen(POSTHEADER);
    strcpy(pcBufHeaders, HOSTHEADER);  pcBufHeaders += strlen(HOSTHEADER);
    strcpy(pcBufHeaders, CHEADER);     pcBufHeaders += strlen(CHEADER);
    strcpy(pcBufHeaders, CTHEADER);    pcBufHeaders += strlen(CTHEADER);
    strcpy(pcBufHeaders, CLHEADER1);   pcBufHeaders += strlen(CLHEADER1);
    snprintf(cCLLength, sizeof(cCLLength), "%d", bodyLen);
    strcpy(pcBufHeaders, cCLLength);   pcBufHeaders += strlen(cCLLength);
    strcpy(pcBufHeaders, CLHEADER2);   pcBufHeaders += strlen(CLHEADER2);
    strcpy(pcBufHeaders, body);        pcBufHeaders += bodyLen;

    UART_PRINT("HTTP POST payload:\r\n%s\r\n", acSendBuff);

    lRetVal = sl_Send(iTLSSockID, acSendBuff, strlen(acSendBuff), 0);
    if (lRetVal < 0)
    {
        UART_PRINT("POST send failed: %d\r\n", lRetVal);
        sl_Close(iTLSSockID);
        return lRetVal;
    }

    lRetVal = sl_Recv(iTLSSockID, acRecvbuff, sizeof(acRecvbuff) - 1, 0);
    if (lRetVal < 0)
    {
        UART_PRINT("POST recv failed: %d\r\n", lRetVal);
        return lRetVal;
    }
    acRecvbuff[lRetVal] = '\0';
    UART_PRINT("Server response:\r\n%s\r\n\r\n", acRecvbuff);

    /* Return 0 on HTTP 200, negative otherwise */
    return (strstr(acRecvbuff, "200") != NULL) ? 0 : -1;
}

/* -----------------------------------------------------------------------
 * OLED screen helpers
 * ----------------------------------------------------------------------- */
static void Screen_Idle(void)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(10, 10, "FITNESS TRAINER", OLED_CYAN,  OLED_BLACK);
    OLED_DrawString(18, 30, "Press SW3",       OLED_WHITE, OLED_BLACK);
    OLED_DrawString(18, 42, "to start",        OLED_WHITE, OLED_BLACK);
}

static void Screen_ModeSelect(WorkoutMode_t mode)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(14, 10, "SELECT MODE",     OLED_YELLOW, OLED_BLACK);
    OLED_DrawString(10, 36, "SW2: cycle mode", OLED_GRAY,   OLED_BLACK);
    OLED_DrawString(10, 48, "SW3: confirm",    OLED_GRAY,   OLED_BLACK);
    if (mode == MODE_PUSHUP)
    {
        OLED_DrawRect(10, 60, 108, 18, OLED_GREEN);
        OLED_DrawString(22, 64, "> PUSH-UPS <", OLED_BLACK, OLED_GREEN);
        OLED_DrawString(28, 84,  "  Running  ",  OLED_WHITE, OLED_BLACK);
    }
    else
    {
        OLED_DrawString(28, 60,  "  Push-ups  ", OLED_WHITE, OLED_BLACK);
        OLED_DrawRect(10, 78, 108, 18, OLED_GREEN);
        OLED_DrawString(22, 82, "> RUNNING  <", OLED_BLACK, OLED_GREEN);
    }
}

static void Screen_TrackingPushup(uint32_t reps)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(20, 6,  "PUSH-UP MODE", OLED_CYAN,  OLED_BLACK);
    OLED_DrawString(10, 24, "Reps:",        OLED_WHITE, OLED_BLACK);
    OLED_Printf    (46, 24, OLED_GREEN, OLED_BLACK, "%lu", reps);
    OLED_DrawString(10, 110,"SW2: Stop",    OLED_GRAY,  OLED_BLACK);
}

static void Screen_TrackingRunning(uint32_t steps)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(16, 6,  "RUNNING MODE", OLED_CYAN,  OLED_BLACK);
    OLED_DrawString(10, 24, "Steps:",       OLED_WHITE, OLED_BLACK);
    OLED_Printf    (52, 24, OLED_GREEN, OLED_BLACK, "%lu", steps);
    OLED_DrawString(10, 110,"SW2: Stop",    OLED_GRAY,  OLED_BLACK);
}

static void Screen_Feedback(WorkoutMode_t mode, uint32_t count,
                             uint32_t duration_s)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(20, 6, "WORKOUT DONE!", OLED_YELLOW, OLED_BLACK);
    if (mode == MODE_PUSHUP)
        OLED_Printf(10, 30, OLED_WHITE, OLED_BLACK, "Reps:  %lu", count);
    else
        OLED_Printf(10, 30, OLED_WHITE, OLED_BLACK, "Steps: %lu", count);
    OLED_Printf(10, 46, OLED_WHITE, OLED_BLACK, "Time:  %lus", duration_s);
    OLED_DrawString(10, 70, "Uploading...", OLED_GRAY, OLED_BLACK);
}

static void Screen_Uploaded(bool ok)
{
    OLED_DrawRect(0, 68, 128, 20, OLED_BLACK);
    if (ok)
        OLED_DrawString(14, 72, "Upload: OK  ", OLED_GREEN, OLED_BLACK);
    else
        OLED_DrawString(10, 72, "Upload: FAIL", OLED_RED,   OLED_BLACK);
    OLED_DrawString(16, 100, "Returning...", OLED_GRAY, OLED_BLACK);
    MAP_UtilsDelay(40000000); /* ~3 s */
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
void main(void)
{
    long lRetVal = -1;

    /* --- Board init (identical to Lab 4) --- */
    BoardInit();
    PinMuxConfig();
    GPIO_Init();
    InitTerm();
    ClearTerm();
    UART_PRINT("Smart Fitness Trainer — booting\r\n");

    /* --- Peripheral init --- */
    I2C_IF_Open(I2C_MASTER_MODE_FST);
    OLED_Init();
    Accel_Init();
    IR_Init();

    /* --- Timer (sensor sampling) --- */
    Timer_Init();

    /* --- Wi-Fi + TLS (same flow as Lab 4) --- */
    OLED_DrawString(8, 100, "Connecting WiFi", OLED_GRAY, OLED_BLACK);

    g_app_config.host = SERVER_NAME;
    g_app_config.port = GOOGLE_DST_PORT;

    lRetVal = connectToAccessPoint();
    UART_PRINT("WiFi connect: %ld\r\n", lRetVal);

    lRetVal = set_time();
    UART_PRINT("Set time: %ld\r\n", lRetVal);
    if (lRetVal < 0)
    {
        UART_PRINT("Unable to set time — TLS uploads disabled\r\n");
    }

    bool wifi_ok = (lRetVal == 0);

    /* --- Show idle screen --- */
    Screen_Idle();

    /* -----------------------------------------------------------------------
     * Main state-machine loop
     * ----------------------------------------------------------------------- */
    uint32_t last_display_tick = 0;
    uint32_t count_display     = 0;

    while (1)
    {
        bool sw2 = false, sw3 = false;

        MAP_IntMasterDisable();
        sw2 = g_sw2_event; g_sw2_event = false;
        sw3 = g_sw3_event; g_sw3_event = false;
        MAP_IntMasterEnable();

        switch (g_state)
        {
            /* ---- IDLE ---- */
            case STATE_IDLE:
                if (sw3)
                {
                    g_mode  = MODE_PUSHUP;
                    g_state = STATE_MODE_SELECT;
                    Screen_ModeSelect(g_mode);
                }
                break;

            /* ---- MODE_SELECT ---- */
            case STATE_MODE_SELECT:
                if (sw2)
                {
                    g_mode = (g_mode == MODE_PUSHUP) ? MODE_RUNNING : MODE_PUSHUP;
                    Screen_ModeSelect(g_mode);
                }
                if (sw3)
                {
                    if (g_mode == MODE_PUSHUP)
                    {
                        IR_ResetRepCount();
                        g_state = STATE_PUSHUP_TRACK;
                        Screen_TrackingPushup(0);
                    }
                    else
                    {
                        Accel_ResetStepCount();
                        g_state = STATE_RUNNING_TRACK;
                        Screen_TrackingRunning(0);
                    }
                    g_session_start   = g_tick_ms;
                    last_display_tick = g_tick_ms;
                    count_display     = 0;
                }
                break;

            /* ---- PUSHUP_TRACK ---- */
            case STATE_PUSHUP_TRACK:
            {
                uint32_t reps = IR_GetRepCount();
                uint32_t now  = g_tick_ms;

                if ((now - last_display_tick) >= 500 || reps != count_display)
                {
                    Screen_TrackingPushup(reps);
                    last_display_tick = now;
                    count_display     = reps;
                }

                uint32_t elapsed_s = (now - g_session_start) / 1000;
                if (sw2 || elapsed_s >= SESSION_TIMEOUT_S)
                {
                    g_state = STATE_FEEDBACK;
                    Screen_Feedback(MODE_PUSHUP, reps, elapsed_s);
                }
                break;
            }

            /* ---- RUNNING_TRACK ---- */
            case STATE_RUNNING_TRACK:
            {
                uint32_t steps = Accel_GetStepCount();
                uint32_t now   = g_tick_ms;

                if ((now - last_display_tick) >= 500 || steps != count_display)
                {
                    Screen_TrackingRunning(steps);
                    last_display_tick = now;
                    count_display     = steps;
                }

                uint32_t elapsed_s = (now - g_session_start) / 1000;
                if (sw2 || elapsed_s >= SESSION_TIMEOUT_S)
                {
                    g_state = STATE_FEEDBACK;
                    Screen_Feedback(MODE_RUNNING, steps, elapsed_s);
                }
                break;
            }

            /* ---- FEEDBACK → UPLOAD ---- */
            case STATE_FEEDBACK:
            {
                uint32_t final_count = (g_mode == MODE_PUSHUP)
                                       ? IR_GetRepCount()
                                       : Accel_GetStepCount();
                uint32_t duration_s  = (g_tick_ms - g_session_start) / 1000;
                bool ok = false;

                g_state = STATE_UPLOAD;

                if (wifi_ok)
                {
                    /* Open a fresh TLS socket for each upload (same as Lab 4) */
                    lRetVal = tls_connect();
                    UART_PRINT("TLS connect: %ld\r\n", lRetVal);
                    if (lRetVal >= 0)
                    {
                        ok = (http_post_workout(lRetVal, g_mode,
                                                final_count, duration_s) == 0);
                        sl_Close(lRetVal);
                    }
                    else
                    {
                        ERR_PRINT(lRetVal);
                    }
                }

                Screen_Uploaded(ok);
                UART_PRINT("Session done — count=%lu upload=%s\r\n",
                           (unsigned long)final_count, ok ? "OK" : "FAIL");

                g_state = STATE_IDLE;
                Screen_Idle();
                break;
            }

            case STATE_UPLOAD:
            default:
                break;
        }

        MAP_UtilsDelay(80000); /* ~6 ms yield */
    }
}
