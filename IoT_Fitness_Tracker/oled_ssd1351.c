//*****************************************************************************
//
// oled_ssd1351.c
//
// SSD1351 SPI OLED driver for CC3200.
// OLED assumed: SSD1351, SPI interface, 128 x 128 color OLED, RGB565.
//
// Connections:
//   OLED SCLK   -> CC3200 PIN_05 / GSPI CLK
//   OLED DIN    -> CC3200 PIN_07 / GSPI MOSI
//   OLED MISO   -> CC3200 PIN_06 / unused, configured
//   OLED CS     -> CC3200 PIN_08 / GSPI CS
//   OLED D/C    -> CC3200 PIN_61 / GPIOA0 bit 6
//   OLED RESET  -> CC3200 PIN_62 / GPIOA0 bit 7
//
//*****************************************************************************

#include <stdio.h>

#include "hw_types.h"
#include "hw_memmap.h"
#include "rom_map.h"
#include "prcm.h"
#include "utils.h"
#include "gpio.h"
#include "pin.h"
#include "spi.h"

#include "oled_ssd1351.h"

//*****************************************************************************
// SSD1351 OLED DEFINITIONS
//*****************************************************************************

#define OLED_SPI_BIT_RATE            1000000

/*
 * Your table:
 *   PIN_61 = D/C
 *   PIN_62 = RESET
 *
 * These are GPIOA0 bit 6 and bit 7.
 */
#define OLED_GPIO_PORT               GPIOA0_BASE
#define OLED_DC_PIN                  0x40
#define OLED_RESET_PIN               0x80

#define SSD1351_WIDTH                128
#define SSD1351_HEIGHT               128

/*
 * Most 128x128 SSD1351 modules use 0,0 offset.
 * If image is shifted, change these to 2 or another small offset.
 */
#define SSD1351_COL_OFFSET           0
#define SSD1351_ROW_OFFSET           0

//*****************************************************************************
// LOW LEVEL FUNCTIONS
//*****************************************************************************

static void OLED_Delay(void)
{
    MAP_UtilsDelay(800000);
}

static void OLED_DC_Command(void)
{
    MAP_GPIOPinWrite(OLED_GPIO_PORT, OLED_DC_PIN, 0);
}

static void OLED_DC_Data(void)
{
    MAP_GPIOPinWrite(OLED_GPIO_PORT, OLED_DC_PIN, OLED_DC_PIN);
}

static void OLED_Reset_Low(void)
{
    MAP_GPIOPinWrite(OLED_GPIO_PORT, OLED_RESET_PIN, 0);
}

static void OLED_Reset_High(void)
{
    MAP_GPIOPinWrite(OLED_GPIO_PORT, OLED_RESET_PIN, OLED_RESET_PIN);
}

static void OLED_SPI_WriteByte_NoCS(unsigned char value)
{
    unsigned long dummy;

    MAP_SPIDataPut(GSPI_BASE, value);
    MAP_SPIDataGet(GSPI_BASE, &dummy);
}

static void SSD1351_Command(unsigned char cmd)
{
    OLED_DC_Command();

    MAP_SPICSEnable(GSPI_BASE);
    OLED_SPI_WriteByte_NoCS(cmd);
    MAP_SPICSDisable(GSPI_BASE);
}

static void SSD1351_DataByte(unsigned char data)
{
    OLED_DC_Data();

    MAP_SPICSEnable(GSPI_BASE);
    OLED_SPI_WriteByte_NoCS(data);
    MAP_SPICSDisable(GSPI_BASE);
}

static void SSD1351_CommandData1(unsigned char cmd, unsigned char data)
{
    SSD1351_Command(cmd);
    SSD1351_DataByte(data);
}

static void SSD1351_SetAddrWindow(unsigned char x0,
                                  unsigned char y0,
                                  unsigned char x1,
                                  unsigned char y1)
{
    SSD1351_Command(0x15);        // Set column address
    SSD1351_DataByte(x0 + SSD1351_COL_OFFSET);
    SSD1351_DataByte(x1 + SSD1351_COL_OFFSET);

    SSD1351_Command(0x75);        // Set row address
    SSD1351_DataByte(y0 + SSD1351_ROW_OFFSET);
    SSD1351_DataByte(y1 + SSD1351_ROW_OFFSET);

    SSD1351_Command(0x5C);        // Write RAM
}

static void SSD1351_WriteColor_NoCS(unsigned int color)
{
    unsigned char high_byte;
    unsigned char low_byte;

    high_byte = (unsigned char)((color >> 8) & 0xFF);
    low_byte  = (unsigned char)(color & 0xFF);

    OLED_SPI_WriteByte_NoCS(high_byte);
    OLED_SPI_WriteByte_NoCS(low_byte);
}

static void SSD1351_FillRect(unsigned char x,
                             unsigned char y,
                             unsigned char w,
                             unsigned char h,
                             unsigned int color)
{
    unsigned long pixels;
    unsigned long i;

    if(x >= SSD1351_WIDTH)
    {
        return;
    }

    if(y >= SSD1351_HEIGHT)
    {
        return;
    }

    if((x + w) > SSD1351_WIDTH)
    {
        w = SSD1351_WIDTH - x;
    }

    if((y + h) > SSD1351_HEIGHT)
    {
        h = SSD1351_HEIGHT - y;
    }

    pixels = (unsigned long)w * (unsigned long)h;

    SSD1351_SetAddrWindow(x, y, x + w - 1, y + h - 1);

    OLED_DC_Data();

    MAP_SPICSEnable(GSPI_BASE);

    for(i = 0; i < pixels; i++)
    {
        SSD1351_WriteColor_NoCS(color);
    }

    MAP_SPICSDisable(GSPI_BASE);
}

static void SSD1351_FillScreen(unsigned int color)
{
    SSD1351_FillRect(0, 0, SSD1351_WIDTH, SSD1351_HEIGHT, color);
}

//*****************************************************************************
// PUBLIC OLED INIT FUNCTIONS
//*****************************************************************************

void OLED_PinMuxConfig(void)
{
    MAP_PRCMPeripheralClkEnable(PRCM_GSPI, PRCM_RUN_MODE_CLK);
    MAP_PRCMPeripheralClkEnable(PRCM_GPIOA0, PRCM_RUN_MODE_CLK);

    /*
     * SPI pins from your table.
     */
    MAP_PinTypeSPI(PIN_05, PIN_MODE_7);       // GSPI CLK
    MAP_PinTypeSPI(PIN_06, PIN_MODE_7);       // GSPI MISO, unused by OLED
    MAP_PinTypeSPI(PIN_07, PIN_MODE_7);       // GSPI MOSI / OLED DIN
    MAP_PinTypeSPI(PIN_08, PIN_MODE_7);       // GSPI CS

    /*
     * OLED D/C and RESET.
     */
    MAP_PinTypeGPIO(PIN_61, PIN_MODE_0, 0);
    MAP_PinTypeGPIO(PIN_62, PIN_MODE_0, 0);

    MAP_GPIODirModeSet(OLED_GPIO_PORT,
                       OLED_DC_PIN | OLED_RESET_PIN,
                       GPIO_DIR_MODE_OUT);

    MAP_GPIOPinWrite(OLED_GPIO_PORT,
                     OLED_DC_PIN | OLED_RESET_PIN,
                     OLED_DC_PIN | OLED_RESET_PIN);
}

void OLED_SPI_Init(void)
{
    MAP_PRCMPeripheralReset(PRCM_GSPI);

    MAP_SPIReset(GSPI_BASE);

    MAP_SPIConfigSetExpClk(GSPI_BASE,
                           MAP_PRCMPeripheralClockGet(PRCM_GSPI),
                           OLED_SPI_BIT_RATE,
                           SPI_MODE_MASTER,
                           SPI_SUB_MODE_3,
                           SPI_SW_CTRL_CS |
                           SPI_4PIN_MODE |
                           SPI_TURBO_OFF |
                           SPI_CS_ACTIVELOW |
                           SPI_WL_8);

    MAP_SPIEnable(GSPI_BASE);
}

void OLED_Init(void)
{
    OLED_Reset_High();
    OLED_Delay();

    OLED_Reset_Low();
    OLED_Delay();

    OLED_Reset_High();
    OLED_Delay();

    /*
     * SSD1351 initialization sequence for 128x128 RGB OLED.
     */
    SSD1351_CommandData1(0xFD, 0x12);     // Command lock
    SSD1351_CommandData1(0xFD, 0xB1);     // Command unlock

    SSD1351_Command(0xAE);                // Display off

    SSD1351_CommandData1(0xB3, 0xF1);     // Clock divider
    SSD1351_CommandData1(0xCA, 0x7F);     // MUX ratio = 127
    SSD1351_CommandData1(0xA0, 0x74);     // Remap, RGB565, 65k color
    SSD1351_CommandData1(0xA1, 0x00);     // Display start line
    SSD1351_CommandData1(0xA2, 0x00);     // Display offset
    SSD1351_CommandData1(0xB5, 0x00);     // GPIO
    SSD1351_CommandData1(0xAB, 0x01);     // Function select

    SSD1351_CommandData1(0xB1, 0x32);     // Phase length
    SSD1351_CommandData1(0xBE, 0x05);     // VCOMH voltage

    SSD1351_Command(0xC1);                // Contrast ABC
    SSD1351_DataByte(0xC8);
    SSD1351_DataByte(0x80);
    SSD1351_DataByte(0xC8);

    SSD1351_CommandData1(0xC7, 0x0F);     // Master contrast

    SSD1351_Command(0xB4);                // Set VSL
    SSD1351_DataByte(0xA0);
    SSD1351_DataByte(0xB5);
    SSD1351_DataByte(0x55);

    SSD1351_CommandData1(0xB6, 0x01);     // Second pre-charge period

    SSD1351_Command(0xA6);                // Normal display
    SSD1351_Command(0xAF);                // Display on

    SSD1351_FillScreen(COLOR_BLACK);
}

//*****************************************************************************
// SIMPLE 5x7 FONT
//*****************************************************************************

static void OLED_GetCharPattern(char c, unsigned char pattern[5])
{
    unsigned char i;

    for(i = 0; i < 5; i++)
    {
        pattern[i] = 0x00;
    }

    switch(c)
    {
        case '0':
            pattern[0] = 0x3E; pattern[1] = 0x51; pattern[2] = 0x49; pattern[3] = 0x45; pattern[4] = 0x3E;
            break;
        case '1':
            pattern[0] = 0x00; pattern[1] = 0x42; pattern[2] = 0x7F; pattern[3] = 0x40; pattern[4] = 0x00;
            break;
        case '2':
            pattern[0] = 0x42; pattern[1] = 0x61; pattern[2] = 0x51; pattern[3] = 0x49; pattern[4] = 0x46;
            break;
        case '3':
            pattern[0] = 0x21; pattern[1] = 0x41; pattern[2] = 0x45; pattern[3] = 0x4B; pattern[4] = 0x31;
            break;
        case '4':
            pattern[0] = 0x18; pattern[1] = 0x14; pattern[2] = 0x12; pattern[3] = 0x7F; pattern[4] = 0x10;
            break;
        case '5':
            pattern[0] = 0x27; pattern[1] = 0x45; pattern[2] = 0x45; pattern[3] = 0x45; pattern[4] = 0x39;
            break;
        case '6':
            pattern[0] = 0x3C; pattern[1] = 0x4A; pattern[2] = 0x49; pattern[3] = 0x49; pattern[4] = 0x30;
            break;
        case '7':
            pattern[0] = 0x01; pattern[1] = 0x71; pattern[2] = 0x09; pattern[3] = 0x05; pattern[4] = 0x03;
            break;
        case '8':
            pattern[0] = 0x36; pattern[1] = 0x49; pattern[2] = 0x49; pattern[3] = 0x49; pattern[4] = 0x36;
            break;
        case '9':
            pattern[0] = 0x06; pattern[1] = 0x49; pattern[2] = 0x49; pattern[3] = 0x29; pattern[4] = 0x1E;
            break;

        case 'A':
            pattern[0] = 0x7E; pattern[1] = 0x11; pattern[2] = 0x11; pattern[3] = 0x11; pattern[4] = 0x7E;
            break;
        case 'B':
            pattern[0] = 0x7F; pattern[1] = 0x49; pattern[2] = 0x49; pattern[3] = 0x49; pattern[4] = 0x36;
            break;
        case 'C':
            pattern[0] = 0x3E; pattern[1] = 0x41; pattern[2] = 0x41; pattern[3] = 0x41; pattern[4] = 0x22;
            break;
        case 'D':
            pattern[0] = 0x7F; pattern[1] = 0x41; pattern[2] = 0x41; pattern[3] = 0x22; pattern[4] = 0x1C;
            break;
        case 'E':
            pattern[0] = 0x7F; pattern[1] = 0x49; pattern[2] = 0x49; pattern[3] = 0x49; pattern[4] = 0x41;
            break;
        case 'F':
            pattern[0] = 0x7F; pattern[1] = 0x09; pattern[2] = 0x09; pattern[3] = 0x09; pattern[4] = 0x01;
            break;
        case 'G':
            pattern[0] = 0x3E; pattern[1] = 0x41; pattern[2] = 0x49; pattern[3] = 0x49; pattern[4] = 0x7A;
            break;
        case 'H':
            pattern[0] = 0x7F; pattern[1] = 0x08; pattern[2] = 0x08; pattern[3] = 0x08; pattern[4] = 0x7F;
            break;
        case 'I':
            pattern[0] = 0x00; pattern[1] = 0x41; pattern[2] = 0x7F; pattern[3] = 0x41; pattern[4] = 0x00;
            break;
        case 'J':
            pattern[0] = 0x20; pattern[1] = 0x40; pattern[2] = 0x41; pattern[3] = 0x3F; pattern[4] = 0x01;
            break;
        case 'K':
            pattern[0] = 0x7F; pattern[1] = 0x08; pattern[2] = 0x14; pattern[3] = 0x22; pattern[4] = 0x41;
            break;
        case 'L':
            pattern[0] = 0x7F; pattern[1] = 0x40; pattern[2] = 0x40; pattern[3] = 0x40; pattern[4] = 0x40;
            break;
        case 'M':
            pattern[0] = 0x7F; pattern[1] = 0x02; pattern[2] = 0x0C; pattern[3] = 0x02; pattern[4] = 0x7F;
            break;
        case 'N':
            pattern[0] = 0x7F; pattern[1] = 0x04; pattern[2] = 0x08; pattern[3] = 0x10; pattern[4] = 0x7F;
            break;
        case 'O':
            pattern[0] = 0x3E; pattern[1] = 0x41; pattern[2] = 0x41; pattern[3] = 0x41; pattern[4] = 0x3E;
            break;
        case 'P':
            pattern[0] = 0x7F; pattern[1] = 0x09; pattern[2] = 0x09; pattern[3] = 0x09; pattern[4] = 0x06;
            break;
        case 'Q':
            pattern[0] = 0x3E; pattern[1] = 0x41; pattern[2] = 0x51; pattern[3] = 0x21; pattern[4] = 0x5E;
            break;
        case 'R':
            pattern[0] = 0x7F; pattern[1] = 0x09; pattern[2] = 0x19; pattern[3] = 0x29; pattern[4] = 0x46;
            break;
        case 'S':
            pattern[0] = 0x46; pattern[1] = 0x49; pattern[2] = 0x49; pattern[3] = 0x49; pattern[4] = 0x31;
            break;
        case 'T':
            pattern[0] = 0x01; pattern[1] = 0x01; pattern[2] = 0x7F; pattern[3] = 0x01; pattern[4] = 0x01;
            break;
        case 'U':
            pattern[0] = 0x3F; pattern[1] = 0x40; pattern[2] = 0x40; pattern[3] = 0x40; pattern[4] = 0x3F;
            break;
        case 'V':
            pattern[0] = 0x1F; pattern[1] = 0x20; pattern[2] = 0x40; pattern[3] = 0x20; pattern[4] = 0x1F;
            break;
        case 'W':
            pattern[0] = 0x7F; pattern[1] = 0x20; pattern[2] = 0x18; pattern[3] = 0x20; pattern[4] = 0x7F;
            break;
        case 'X':
            pattern[0] = 0x63; pattern[1] = 0x14; pattern[2] = 0x08; pattern[3] = 0x14; pattern[4] = 0x63;
            break;
        case 'Y':
            pattern[0] = 0x07; pattern[1] = 0x08; pattern[2] = 0x70; pattern[3] = 0x08; pattern[4] = 0x07;
            break;
        case 'Z':
            pattern[0] = 0x61; pattern[1] = 0x51; pattern[2] = 0x49; pattern[3] = 0x45; pattern[4] = 0x43;
            break;

        case ':':
            pattern[0] = 0x00; pattern[1] = 0x36; pattern[2] = 0x36; pattern[3] = 0x00; pattern[4] = 0x00;
            break;
        case '.':
            pattern[0] = 0x00; pattern[1] = 0x60; pattern[2] = 0x60; pattern[3] = 0x00; pattern[4] = 0x00;
            break;
        case '-':
            pattern[0] = 0x08; pattern[1] = 0x08; pattern[2] = 0x08; pattern[3] = 0x08; pattern[4] = 0x08;
            break;
        case ' ':
        default:
            break;
    }
}

static void OLED_DrawChar(unsigned char x,
                          unsigned char y,
                          char c,
                          unsigned char scale,
                          unsigned int fg_color,
                          unsigned int bg_color)
{
    unsigned char pattern[5];
    unsigned char col;
    unsigned char row;
    unsigned int color;

    OLED_GetCharPattern(c, pattern);

    for(col = 0; col < 5; col++)
    {
        for(row = 0; row < 7; row++)
        {
            if(pattern[col] & (1 << row))
            {
                color = fg_color;
            }
            else
            {
                color = bg_color;
            }

            SSD1351_FillRect((unsigned char)(x + (col * scale)),
                             (unsigned char)(y + (row * scale)),
                             scale,
                             scale,
                             color);
        }
    }

    SSD1351_FillRect((unsigned char)(x + (5 * scale)),
                     y,
                     scale,
                     (unsigned char)(7 * scale),
                     bg_color);
}

static void OLED_DrawString(unsigned char x,
                            unsigned char y,
                            const char *str,
                            unsigned char scale,
                            unsigned int fg_color,
                            unsigned int bg_color)
{
    while(*str != '\0')
    {
        OLED_DrawChar(x, y, *str, scale, fg_color, bg_color);

        x = (unsigned char)(x + (6 * scale));

        if(x > (SSD1351_WIDTH - (6 * scale)))
        {
            break;
        }

        str++;
    }
}

//*****************************************************************************
// PUBLIC OLED DISPLAY SCREENS
//*****************************************************************************

void OLED_ShowStartupScreen(void)
{
    SSD1351_FillScreen(COLOR_BLACK);

    OLED_DrawString(4, 8,  "CC3200", 2, COLOR_CYAN, COLOR_BLACK);
    OLED_DrawString(4, 32, "BMA222", 2, COLOR_WHITE, COLOR_BLACK);
    OLED_DrawString(4, 56, "STEP",   2, COLOR_GREEN, COLOR_BLACK);
    OLED_DrawString(4, 80, "COUNTER",2, COLOR_GREEN, COLOR_BLACK);
}

void OLED_ShowCalibrationScreen(unsigned int current, unsigned int total)
{
    char buffer[20];

    SSD1351_FillScreen(COLOR_BLACK);

    OLED_DrawString(4, 8, "CALIBRATING", 1, COLOR_YELLOW, COLOR_BLACK);
    OLED_DrawString(4, 28, "STAND STILL", 1, COLOR_WHITE, COLOR_BLACK);

    sprintf(buffer, "%u/%u", current, total);
    OLED_DrawString(4, 58, buffer, 2, COLOR_CYAN, COLOR_BLACK);
}

void OLED_ShowStepCount(unsigned long steps)
{
    char buffer[16];

    /*
     * Full screen clear is simple and reliable.
     * Later this can be optimized to only erase the number area.
     */
    SSD1351_FillScreen(COLOR_BLACK);

    OLED_DrawString(4, 10, "STEPS:", 2, COLOR_WHITE, COLOR_BLACK);

    sprintf(buffer, "%lu", steps);

    OLED_DrawString(4, 55, buffer, 4, COLOR_GREEN, COLOR_BLACK);
}

void OLED_ShowError(const char *line1, const char *line2)
{
    SSD1351_FillScreen(COLOR_BLACK);

    OLED_DrawString(4, 20, line1, 1, COLOR_RED, COLOR_BLACK);
    OLED_DrawString(4, 42, line2, 1, COLOR_WHITE, COLOR_BLACK);
}

void OLED_ShowHalloWorld(void)
{
    SSD1351_FillScreen(COLOR_BLACK);

    OLED_DrawString(8, 35, "HALLO", 3, COLOR_CYAN, COLOR_BLACK);
    OLED_DrawString(8, 70, "WORLD", 3, COLOR_GREEN, COLOR_BLACK);
}

void OLED_ShowStepMode(unsigned long steps)
{
    char buffer[16];

    SSD1351_FillScreen(COLOR_BLACK);

    OLED_DrawString(4, 8, "STEP", 2, COLOR_CYAN, COLOR_BLACK);
    OLED_DrawString(4, 32, "MODE", 2, COLOR_WHITE, COLOR_BLACK);

    OLED_DrawString(4, 62, "STEPS:", 1, COLOR_YELLOW, COLOR_BLACK);

    sprintf(buffer, "%lu", steps);
    OLED_DrawString(4, 82, buffer, 3, COLOR_GREEN, COLOR_BLACK);
}

void OLED_ShowPushupMode(unsigned long pushups)
{
    char buffer[16];

    SSD1351_FillScreen(COLOR_BLACK);

    OLED_DrawString(4, 8, "PUSHUP", 2, COLOR_MAGENTA, COLOR_BLACK);
    OLED_DrawString(4, 32, "MODE", 2, COLOR_WHITE, COLOR_BLACK);

    OLED_DrawString(4, 62, "COUNT:", 1, COLOR_YELLOW, COLOR_BLACK);

    sprintf(buffer, "%lu", pushups);
    OLED_DrawString(4, 82, buffer, 3, COLOR_GREEN, COLOR_BLACK);
}
