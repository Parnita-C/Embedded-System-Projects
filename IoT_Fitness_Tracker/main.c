/*
 * main.c
 *
 * Smart Interactive IoT Fitness Trainer
 * CC3200 LaunchXL  CC3200SDK_1.4.0
 *
 * Authors: Parnita Chowtoori, Selena Phu  |  EEC 172 - A03
 *
 * State Machine:
 *   IDLE  MODE_SELECT  (PUSHUP_TRACKING | RUNNING_TRACKING)
 *         FEEDBACK  UPLOAD  IDLE
 *
 * Timers:
 *   TimerA0: 20 ms ISR  sensor sampling (IR + Accel)
 *   Tick counter driven by TimerA0 for elapsed time
 *
 * Buttons:
 *   SW2 (GPIO 13): in IDLE/MODE_SELECT  cycle mode; in TRACKING  stop
 *   SW3 (GPIO 17): in MODE_SELECT  confirm; in IDLE  start
 */

/* -----------------------------------------------------------------------
 * Includes  CC3200 SDK
 * ----------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* Hardware abstraction */
#include "hw_types.h"
#include "hw_ints.h"
#include "hw_memmap.h"
#include "hw_common_reg.h"

/* SDK drivers */
#include "rom.h"
#include "rom_map.h"
#include "interrupt.h"
#include "prcm.h"
#include "timer.h"
#include "gpio.h"
#include "spi.h"
#include "i2c_if.h"
#include "uart_if.h"
#include "utils_if.h"

/* SimpleLink */
#include "simplelink.h"

/* Project modules */
#include "pin_mux_config.h"
#include "accelerometer.h"
#include "ir_sensor.h"
#include "oled.h"
#include "wifi_upload.h"

/* -----------------------------------------------------------------------
 * Configuration
 * ----------------------------------------------------------------------- */
#define TIMER_PERIOD_US     20000UL     /* 20 ms sampling interval      */
#define SESSION_TIMEOUT_S   120         /* auto-stop after 2 min        */
#define BTN_DEBOUNCE_TICKS  5           /* ×20 ms = 100 ms debounce     */
#define UART_BAUD           115200


#define UART_PRINT Report

/* -----------------------------------------------------------------------
 * State machine
 * ----------------------------------------------------------------------- */
typedef enum {
    STATE_IDLE           = 0,
    STATE_MODE_SELECT    = 1,
    STATE_PUSHUP_TRACK   = 2,
    STATE_RUNNING_TRACK  = 3,
    STATE_FEEDBACK       = 4,
    STATE_UPLOAD         = 5
} AppState_t;

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */
static volatile AppState_t  g_state          = STATE_IDLE;
static volatile WorkoutMode_t g_mode         = MODE_PUSHUP;
static volatile uint32_t    g_tick_ms        = 0;   /* incremented in ISR */
static volatile uint32_t    g_session_start  = 0;
static volatile bool        g_sw2_event      = false;
static volatile bool        g_sw3_event      = false;
static volatile uint8_t     g_sw2_deb        = 0;
static volatile uint8_t     g_sw3_deb        = 0;

/* -----------------------------------------------------------------------
 * Timer ISR  20 ms
 *   Runs sensor processing and button debounce
 * ----------------------------------------------------------------------- */
void TimerA0ISR(void)
{
    MAP_TimerIntClear(TIMERA0_BASE, TIMER_TIMA_TIMEOUT);
    g_tick_ms += 20;

    /* Sensor processing only during active tracking */
    if (g_state == STATE_PUSHUP_TRACK)
    {
        IR_ProcessRep();
    }
    else if (g_state == STATE_RUNNING_TRACK)
    {
        Accel_ProcessStep(g_tick_ms);
    }

    /* Button debounce  SW2 (active LOW) */
    if (!(GPIOPinRead(SW2_BASE, SW2_PIN)))
    {
        if (g_sw2_deb < BTN_DEBOUNCE_TICKS) g_sw2_deb++;
        else { g_sw2_event = true; g_sw2_deb = 0; }
    }
    else { g_sw2_deb = 0; }

    /* Button debounce  SW3 (active LOW) */
    if (!(GPIOPinRead(SW3_BASE, SW3_PIN)))
    {
        if (g_sw3_deb < BTN_DEBOUNCE_TICKS) g_sw3_deb++;
        else { g_sw3_event = true; g_sw3_deb = 0; }
    }
    else { g_sw3_deb = 0; }
}

/* -----------------------------------------------------------------------
 * Timer Setup
 * ----------------------------------------------------------------------- */
static void Timer_Init(void)
{
    MAP_PRCMPeripheralClkEnable(PRCM_TIMERA0, PRCM_RUN_MODE_CLK);
    MAP_TimerConfigure(TIMERA0_BASE, TIMER_CFG_PERIODIC);

    uint32_t load = MAP_PRCMPeripheralClockGet(PRCM_TIMERA0) /
                    (1000000UL / TIMER_PERIOD_US);
    MAP_TimerLoadSet(TIMERA0_BASE, TIMER_A, load);
    MAP_TimerIntEnable(TIMERA0_BASE, TIMER_TIMA_TIMEOUT);
    MAP_IntEnable(INT_TIMERA0A);
    MAP_IntPrioritySet(INT_TIMERA0A, INT_PRIORITY_LVL_1);
    MAP_TimerEnable(TIMERA0_BASE, TIMER_A);
}

/* -----------------------------------------------------------------------
 * OLED Screen helpers
 * ----------------------------------------------------------------------- */
static void Screen_Idle(void)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(10, 10,  "FITNESS TRAINER", OLED_CYAN,  OLED_BLACK);
    OLED_DrawString(18, 30,  "Press SW3",       OLED_WHITE, OLED_BLACK);
    OLED_DrawString(18, 42,  "to start",        OLED_WHITE, OLED_BLACK);
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
        OLED_DrawString(28, 84,   "  Running  ",  OLED_WHITE, OLED_BLACK);
    }
    else
    {
        OLED_DrawString(28, 60,   "  Push-ups  ", OLED_WHITE, OLED_BLACK);
        OLED_DrawRect(10, 78, 108, 18, OLED_GREEN);
        OLED_DrawString(22, 82, "> RUNNING  <", OLED_BLACK, OLED_GREEN);
    }
}

static void Screen_TrackingPushup(uint32_t reps)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(20, 6,  "PUSH-UP MODE",   OLED_CYAN, OLED_BLACK);
    OLED_DrawString(10, 24, "Reps:",          OLED_WHITE, OLED_BLACK);
    OLED_Printf    (46, 24, OLED_GREEN, OLED_BLACK, "%lu", reps);
    OLED_DrawString(10, 110,"SW2: Stop",      OLED_GRAY,  OLED_BLACK);
}

static void Screen_TrackingRunning(uint32_t steps)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(16, 6,  "RUNNING MODE",  OLED_CYAN,  OLED_BLACK);
    OLED_DrawString(10, 24, "Steps:",        OLED_WHITE, OLED_BLACK);
    OLED_Printf    (52, 24, OLED_GREEN, OLED_BLACK, "%lu", steps);
    OLED_DrawString(10, 110,"SW2: Stop",     OLED_GRAY,  OLED_BLACK);
}

static void Screen_Feedback(WorkoutMode_t mode, uint32_t count,
                             uint32_t duration_s)
{
    OLED_FillScreen(OLED_BLACK);
    OLED_DrawString(20, 6, "WORKOUT DONE!", OLED_YELLOW, OLED_BLACK);
    if (mode == MODE_PUSHUP)
    {
        OLED_Printf(10, 30, OLED_WHITE, OLED_BLACK, "Reps:  %lu", count);
    }
    else
    {
        OLED_Printf(10, 30, OLED_WHITE, OLED_BLACK, "Steps: %lu", count);
    }
    OLED_Printf(10, 46, OLED_WHITE, OLED_BLACK, "Time:  %lus", duration_s);
    OLED_DrawString(10, 70, "Uploading...", OLED_GRAY, OLED_BLACK);
}

static void Screen_Uploaded(bool ok)
{
    OLED_DrawRect(0, 68, 128, 20, OLED_BLACK);
    if (ok)
        OLED_DrawString(14, 72, "Upload: OK  ", OLED_GREEN,  OLED_BLACK);
    else
        OLED_DrawString(10, 72, "Upload: FAIL", OLED_RED,    OLED_BLACK);
    OLED_DrawString(16, 100, "Returning...", OLED_GRAY, OLED_BLACK);
    MAP_UtilsDelay(40000000); /* ~3 s */
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    /* Board-level init */

    PRCMCC3200MCUInit();

    /* Configure all pins */
    PinMuxConfig();

    /* UART for debug */
    InitTerm();
    ClearTerm();
    UART_PRINT("Smart Fitness Trainer  booting\r\n");

    /* Peripheral init */
    I2C_IF_Open(I2C_MASTER_MODE_FST);   /* 400 kHz I2C */
    OLED_Init();
    Accel_Init();
    IR_Init();

    /* Timer (sensor sampling) */
    Timer_Init();

    MAP_IntMasterEnable();

    /* Wi-Fi connect */
    OLED_DrawString(14, 100, "Connecting WiFi", OLED_GRAY, OLED_BLACK);
    bool wifi_ok = WiFi_Connect();
    UART_PRINT("WiFi: %s\r\n", wifi_ok ? "connected" : "offline");

    /* Show idle screen */
    Screen_Idle();

    /* -----------------------------------------------------------------------
     * Main state-machine loop
     * ----------------------------------------------------------------------- */
    uint32_t last_display_tick = 0;
    uint32_t count_display     = 0;

    while (1)
    {
        bool sw2 = false, sw3 = false;

        /* Atomically grab button events */
        MAP_IntMasterDisable();
        sw2 = g_sw2_event; g_sw2_event = false;
        sw3 = g_sw3_event; g_sw3_event = false;
        MAP_IntMasterEnable();

        switch (g_state)
        {
            /* ----------------------------------------------------------
             * IDLE: wait for SW3 press to enter mode select
             * ---------------------------------------------------------- */
            case STATE_IDLE:
                if (sw3)
                {
                    g_mode  = MODE_PUSHUP;   /* default mode */
                    g_state = STATE_MODE_SELECT;
                    Screen_ModeSelect(g_mode);
                }
                break;

            /* ----------------------------------------------------------
             * MODE_SELECT: SW2 cycles, SW3 confirms
             * ---------------------------------------------------------- */
            case STATE_MODE_SELECT:
                if (sw2)
                {
                    g_mode = (g_mode == MODE_PUSHUP) ? MODE_RUNNING : MODE_PUSHUP;
                    Screen_ModeSelect(g_mode);
                }
                if (sw3)
                {
                    /* Start tracking */
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
                    g_session_start  = g_tick_ms;
                    last_display_tick = g_tick_ms;
                    count_display     = 0;
                }
                break;

            /* ----------------------------------------------------------
             * PUSHUP_TRACK: display updated every 500 ms; SW2 stops
             * ---------------------------------------------------------- */
            case STATE_PUSHUP_TRACK:
            {
                uint32_t reps = IR_GetRepCount();
                uint32_t now  = g_tick_ms;

                /* Refresh display every 500 ms or when count changes */
                if ((now - last_display_tick) >= 500 || reps != count_display)
                {
                    Screen_TrackingPushup(reps);
                    last_display_tick = now;
                    count_display     = reps;
                }

                /* Session timeout */
                uint32_t elapsed_s = (now - g_session_start) / 1000;
                if (sw2 || elapsed_s >= SESSION_TIMEOUT_S)
                {
                    g_state = STATE_FEEDBACK;
                    Screen_Feedback(MODE_PUSHUP, reps, elapsed_s);
                }
                break;
            }

            /* ----------------------------------------------------------
             * RUNNING_TRACK: display updated every 500 ms; SW2 stops
             * ---------------------------------------------------------- */
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

            /* ----------------------------------------------------------
             * FEEDBACK  UPLOAD
             * ---------------------------------------------------------- */
            case STATE_FEEDBACK:
            {
                uint32_t final_count = (g_mode == MODE_PUSHUP)
                                        ? IR_GetRepCount()
                                        : Accel_GetStepCount();
                g_state = STATE_UPLOAD;
                bool ok = wifi_ok
                          ? WiFi_UploadWorkout(g_mode, final_count)
                          : false;
                Screen_Uploaded(ok);
                UART_PRINT("Session complete count=%lu upload=%s\r\n",
                           final_count, ok?"OK":"FAIL");
                g_state = STATE_IDLE;
                Screen_Idle();
                break;
            }

            /* ----------------------------------------------------------
             * UPLOAD handled inline in FEEDBACK above
             * ---------------------------------------------------------- */
            case STATE_UPLOAD:
            default:
                break;
        }

        /* Yield CPU briefly */
        MAP_UtilsDelay(80000);  /* ~6 ms */
    }

    return 0; /* never reached */
}
