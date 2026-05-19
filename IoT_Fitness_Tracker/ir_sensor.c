/*
 * ir_sensor.c
 *
 * IR Proximity Sensor Driver — CC3200 Smart Fitness Trainer
 *
 * The IR module outputs a digital LOW when an object is within range.
 * Connected to GPIO 22 (physical Pin 15, P2 header).
 *
 * Push-up State Machine:
 *   TOP  (IR not triggered) → user is at the top of push-up
 *   DOWN (IR triggered)     → user has lowered to bottom
 *   A rep is counted on the transition: DOWN → TOP
 */

#include <stdbool.h>
#include <stdint.h>
/* hw_types.h must come first — defines tBoolean used by driverlib headers */
#include "hw_types.h"
#include "hw_memmap.h"
#include "ir_sensor.h"
#include "gpio.h"
#include "pin_mux_config.h"

/* Debounce: require signal stable for N consecutive reads (~20 ms each) */
#define DEBOUNCE_COUNT      3

typedef enum {
    PUSHUP_TOP  = 0,
    PUSHUP_DOWN = 1
} PushupState_t;

static PushupState_t  g_pushup_state   = PUSHUP_TOP;
static volatile uint32_t g_rep_count   = 0;
static uint8_t        g_debounce_ctr   = 0;

/* -----------------------------------------------------------------------
 * IR_Init
 *   GPIO already configured as input with pull-up in PinMuxConfig().
 *   Just reset state here.
 * ----------------------------------------------------------------------- */
void IR_Init(void)
{
    g_pushup_state  = PUSHUP_TOP;
    g_rep_count     = 0;
    g_debounce_ctr  = 0;
}

/* -----------------------------------------------------------------------
 * IR_Read
 *   Returns true when object is detected (active LOW input → true when LOW)
 * ----------------------------------------------------------------------- */
bool IR_Read(void)
{
    uint8_t val = GPIOPinRead(IR_SENSOR_BASE, IR_SENSOR_PIN);
    return (val == 0);   /* active LOW */
}

/* -----------------------------------------------------------------------
 * IR_ProcessRep
 *   Call at regular intervals (~20 ms).
 *   Returns true when a full rep (TOP→DOWN→TOP) is completed.
 * ----------------------------------------------------------------------- */
bool IR_ProcessRep(void)
{
    bool object_near = IR_Read();
    bool rep_counted = false;

    switch (g_pushup_state)
    {
        case PUSHUP_TOP:
            if (object_near)
            {
                /* Debounce: require stable signal */
                g_debounce_ctr++;
                if (g_debounce_ctr >= DEBOUNCE_COUNT)
                {
                    g_pushup_state  = PUSHUP_DOWN;
                    g_debounce_ctr  = 0;
                }
            }
            else
            {
                g_debounce_ctr = 0;
            }
            break;

        case PUSHUP_DOWN:
            if (!object_near)
            {
                g_debounce_ctr++;
                if (g_debounce_ctr >= DEBOUNCE_COUNT)
                {
                    /* Completed a full rep: DOWN → TOP */
                    g_pushup_state  = PUSHUP_TOP;
                    g_debounce_ctr  = 0;
                    g_rep_count++;
                    rep_counted = true;
                }
            }
            else
            {
                g_debounce_ctr = 0;
            }
            break;
    }

    return rep_counted;
}

/* -----------------------------------------------------------------------
 * IR_GetRepCount / IR_ResetRepCount
 * ----------------------------------------------------------------------- */
uint32_t IR_GetRepCount(void)
{
    return g_rep_count;
}

void IR_ResetRepCount(void)
{
    g_rep_count     = 0;
    g_pushup_state  = PUSHUP_TOP;
    g_debounce_ctr  = 0;
}
