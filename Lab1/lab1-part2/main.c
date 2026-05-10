//*****************************************************************************
//
// Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/ 
// 
// 
//  Redistribution and use in source and binary forms, with or without 
//  modification, are permitted provided that the following conditions 
//  are met:
//
//    Redistributions of source code must retain the above copyright 
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the 
//    documentation and/or other materials provided with the   
//    distribution.
//
//    Neither the name of Texas Instruments Incorporated nor the names of
//    its contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
//  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
//  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
//  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
//  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
//  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//*****************************************************************************

//*****************************************************************************
//
// Application Name     - Blinky
// Application Overview - The objective of this application is to showcase the 
//                        GPIO control using Driverlib api calls. The LEDs 
//                        connected to the GPIOs on the LP are used to indicate 
//                        the GPIO output. The GPIOs are driven high-low 
//                        periodically in order to turn on-off the LEDs.
//
//*****************************************************************************

//****************************************************************************
//
//! \addtogroup blinky
//! @{
//
//****************************************************************************

// Standard includes
#include <stdio.h>

// Driverlib includes
#include "hw_types.h"
#include "hw_ints.h"
#include "hw_memmap.h"
#include "hw_common_reg.h"
#include "interrupt.h"
#include "hw_apps_rcm.h"
#include "prcm.h"
#include "rom.h"
#include "rom_map.h"
#include "prcm.h"
#include "gpio.h"
#include "utils.h"
#include "uart.h"
#include "interrupt.h"

// Common interface includes
#include "gpio_if.h"
#include "uart_if.h"

#include "pin_mux_config.h"

#define APPLICATION_VERSION     "1.4.0"


//*****************************************************************************
//                 GLOBAL VARIABLES -- Start
//*****************************************************************************
volatile int g_iCounter = 0;

#if defined(ccs)
extern void (* const g_pfnVectors[])(void);
#endif
#if defined(ewarm)
extern uVectorEntry __vector_table;
#endif
//*****************************************************************************
//                 GLOBAL VARIABLES -- End
//*****************************************************************************


//*****************************************************************************
//                      LOCAL FUNCTION PROTOTYPES                           
//*****************************************************************************
void LEDBlinkyRoutine();
static void BoardInit(void);

//*****************************************************************************
//                      LOCAL FUNCTION DEFINITIONS                         
//*****************************************************************************

//State Machine for SW2 and SW3
//*****************************************************************************
typedef enum {
    MODE_IDLE,
    MODE_SW2,
    MODE_SW3
} tMode;
//*****************************************************************************
#define LED_DELAY 8000000
//*****************************************************************************

#define SW2_GPIO_PORT GPIOA1_BASE
#define SW2_GPIO_PIN  0x20   // PIN_15

#define SW3_GPIO_PORT GPIOA2_BASE
#define SW3_GPIO_PIN  0x40   // PIN_22

#define GPIO18_PORT GPIOA3_BASE
#define GPIO18_PIN  0x04     // PIN_18
//*****************************************************************************

void LEDBinaryDisplay(unsigned char count)
{
    GPIO_IF_LedOff(MCU_ALL_LED_IND);

    if (count & 0x01) GPIO_IF_LedOn(MCU_GREEN_LED_GPIO);
    if (count & 0x02) GPIO_IF_LedOn(MCU_ORANGE_LED_GPIO);
    if (count & 0x04) GPIO_IF_LedOn(MCU_RED_LED_GPIO);
}


void LEDBlinkUnisonOnce(void)
{
    GPIO_IF_LedOn(LED1 | LED2 | LED3);
    MAP_UtilsDelay(LED_DELAY);
    GPIO_IF_LedOff(LED1 | LED2 | LED3);
    MAP_UtilsDelay(LED_DELAY);
}
//*****************************************************************************
//
//! Board Initialization & Configuration
//!
//! \param  None
//!
//! \return None
//
//*****************************************************************************
static void
BoardInit(void)
{
/* In case of TI-RTOS vector table is initialize by OS itself */
#ifndef USE_TIRTOS
    //
    // Set vector table base
    //
#if defined(ccs)
    MAP_IntVTableBaseSet((unsigned long)&g_pfnVectors[0]);
#endif
#if defined(ewarm)
    MAP_IntVTableBaseSet((unsigned long)&__vector_table);
#endif
#endif
    
    //
    // Enable Processor
    //
    MAP_IntMasterEnable();
    MAP_IntEnable(FAULT_SYSTICK);

    PRCMCC3200MCUInit();
}
//****************************************************************************
//
//! Main function
//!
//! \param none
//! 
//! This function  
//!    1. Invokes the LEDBlinkyTask
//!
//! \return None.
//
//****************************************************************************
int
main()
{
    unsigned char count = 0;
    tMode currentMode = MODE_IDLE;
    tMode lastMode = MODE_IDLE;
    //
    // Initailizing the board
    //
    BoardInit();
    //
    // Muxing for Enabling UART_TX and UART_RX.
    //
    PinMuxConfig();
    //
    // Initialising the Terminal.
    //
    InitTerm();
    //
    // Clearing the Terminal.
    //
    ClearTerm();
    Message("\t\t****************************************************\n\r");
    Message("\t\t\t        CC3200 GPIO Application        \n\r");
    Message("\t\t ****************************************************\n\n\n\r");
    Message("\t\t ****************************************************\n\r");
    Message("\t\t  Push SW3 to start LED binary counting  \n\r");
    Message("\t\t    Push SW2 to blink LEDs on and of  \n\r");
    Message("\t\t ****************************************************\n\r");
    Message("\n\n\n\r");


    GPIO_IF_LedConfigure(LED1 | LED2 | LED3);
    GPIO_IF_LedOff(MCU_ALL_LED_IND);

    // Configure GPIO18 as output
    GPIODirModeSet(GPIO18_PORT, GPIO18_PIN, GPIO_DIR_MODE_OUT);
    GPIOPinWrite(GPIO18_PORT, GPIO18_PIN, 0);


    while (1)
    {
        // Poll switches

        if (GPIOPinRead(SW3_GPIO_PORT, SW3_GPIO_PIN) == 0)
            currentMode = MODE_SW3;
        else if (GPIOPinRead(SW2_GPIO_PORT, SW2_GPIO_PIN) == 0)
            currentMode = MODE_SW2;
        else
            currentMode = MODE_IDLE;

        if (currentMode == MODE_IDLE)
        {
            GPIO_IF_LedOff(MCU_ALL_LED_IND);
        }

        //Mode entry messages

        if (currentMode != lastMode)
        {
            if (currentMode == MODE_SW3)
            {
                Message("SW3 pressed\n\r");
                GPIOPinWrite(GPIO18_PORT, GPIO18_PIN, 0); // GPIO18 LOW
                count = 0;
            }
            else if (currentMode == MODE_SW2)
            {
                Message("SW2 pressed\n\r");
                GPIOPinWrite(GPIO18_PORT, GPIO18_PIN, GPIO18_PIN); // GPIO18 HIGH
            }

            lastMode = currentMode;
        }

        //Mode behavior

        if (currentMode == MODE_SW3)
        {
            LEDBinaryDisplay(count);
            MAP_UtilsDelay(LED_DELAY);
            count = (count + 1) & 0x07; // 0–7
        }
        else if (currentMode == MODE_SW2)
        {
            LEDBlinkUnisonOnce();
        }
    }
}

//*****************************************************************************
//
// Close the Doxygen group.
//! @}
//
//*****************************************************************************
