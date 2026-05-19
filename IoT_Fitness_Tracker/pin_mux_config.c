/*
 * pin_mux_config.c
 *
 * Smart IoT Fitness Trainer - CC3200 LaunchXL
 * PINMUX Configuration (CC3200SDK_1.4.0)
 *
 * Pin Assignments:
 *   I2C  - SCL: Pin 1 (Dev Pin 1), SDA: Pin 2 (Dev Pin 2)    Accelerometer (BMA222)
 *   SPI  - CLK: Pin 5, CS: Pin 8, DOUT: Pin 7, DIN: Pin 6    OLED Display (SSD1351)
 *   GPIO - Pin 15 (Dev Pin 22)   IR Sensor Input (digital)
 *   GPIO - Pin 4  (P1, Dev 4)    SW2 (Mode Select Button)
 *   GPIO - Pin 15 (P2, Dev 22)   SW3 (Start/Stop Button)
 *
 * Reference: CC3200 LaunchXL pin_config.pdf (P1/P2/P3/P4 headers)
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
    /* Enable peripheral clocks */
    PRCMPeripheralClkEnable(PRCM_UARTA0, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GSPI,   PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_I2CA0,  PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA0, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA1, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA2, PRCM_RUN_MODE_CLK);
    PRCMPeripheralClkEnable(PRCM_GPIOA3, PRCM_RUN_MODE_CLK);

    /* UART0 — Debug console (Pin 55 = TX, Pin 57 = RX) */
    PinTypeUART(PIN_55, PIN_MODE_3);
    PinTypeUART(PIN_57, PIN_MODE_3);

    /* ---------------------------------------------------------------
     * I2C   Accelerometer (BMA222, onboard)
     *   SCL   Pin 1  (Dev Pin 1)  MODE_1 = I2C SCL
     *   SDA   Pin 2  (Dev Pin 2)  MODE_1 = I2C SDA
     * --------------------------------------------------------------- */
    PinTypeI2C(PIN_01, PIN_MODE_1);   /* I2C SCL */
    PinTypeI2C(PIN_02, PIN_MODE_1);   /* I2C SDA */

    /* ---------------------------------------------------------------
     * SPI   OLED Display (SSD1351 / Adafruit 128x128)
     *   CLK     Pin 5  (P1/Dev 5)   MODE_7 = SPI CLK
     *   CS      Pin 8  (P2/Dev 8)   MODE_7 = SPI CS
     *   DOUT    Pin 7  (P2/Dev 7)   MODE_7 = SPI DOUT (MOSI)
     *   DIN     Pin 6  (P2/Dev 6)   MODE_7 = SPI DIN  (MISO, unused but configured)
     * --------------------------------------------------------------- */
    PinTypeSPI(PIN_05, PIN_MODE_7);   /* SPI CLK  */
    PinTypeSPI(PIN_07, PIN_MODE_7);   /* SPI DOUT (MOSI) */
    PinTypeSPI(PIN_06, PIN_MODE_7);   /* SPI DIN  (MISO) */
    PinTypeSPI(PIN_08, PIN_MODE_7);   /* SPI CS   */

    /* ---------------------------------------------------------------
     * GPIO   OLED control lines (D/C and RESET)
     *   OLED D/C     Pin 61 (P1/Dev 61, GPIO 6)
     *   OLED RESET   Pin 62 (P1/Dev 62, GPIO 7)
     * --------------------------------------------------------------- */
    PinTypeGPIO(PIN_61, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA0_BASE, GPIO_PIN_6, GPIO_DIR_MODE_OUT);

    PinTypeGPIO(PIN_62, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA0_BASE, GPIO_PIN_7, GPIO_DIR_MODE_OUT);

    /* ---------------------------------------------------------------
     * GPIO   IR Proximity Sensor (digital input)
     *   IR_IN   Pin 15 (P2/Dev 22, GPIO 22)
     *   Active LOW when object detected (open-collector IR module)
     * --------------------------------------------------------------- */
    PinTypeGPIO(PIN_15, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA2_BASE, GPIO_PIN_6, GPIO_DIR_MODE_IN);

    /* ---------------------------------------------------------------
     * GPIO   Push Buttons (active LOW, internal pull-up)
     *   SW2 (Mode Select)    Pin 4  (P1/Dev 4,  GPIO 13)
     *   SW3 (Start/Stop)     Pin 16 (P2/Dev 55, GPIO 17)   onboard SW3
     * --------------------------------------------------------------- */
    PinTypeGPIO(PIN_04, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA1_BASE, GPIO_PIN_5, GPIO_DIR_MODE_IN);
    PinConfigSet(PIN_04, PIN_STRENGTH_2MA, PIN_TYPE_STD_PU);  /* pull-up */

    PinTypeGPIO(PIN_16, PIN_MODE_0, false);
    GPIODirModeSet(GPIOA2_BASE, GPIO_PIN_1, GPIO_DIR_MODE_IN);
    PinConfigSet(PIN_16, PIN_STRENGTH_2MA, PIN_TYPE_STD_PU);  /* pull-up */
}
