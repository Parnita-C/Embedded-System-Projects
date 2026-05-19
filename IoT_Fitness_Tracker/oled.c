/*
 * oled.c
 *
 * SSD1351 128x128 OLED Driver — CC3200 Smart Fitness Trainer
 * Uses GSPI (SPI) + GPIO D/C and RESET lines
 *
 * Adapted for CC3200SDK_1.4.0 — references Adafruit_SSD1351 protocol
 */


#include <string.h>
#include <stdio.h>
#include <stdarg.h>
/* hw_types.h must come first — defines tBoolean used by driverlib headers */
#include "hw_types.h"
#include "hw_memmap.h"
#include "prcm.h"
#include "oled.h"
#include "rom.h"
#include "rom_map.h"
#include "spi.h"
#include "gpio.h"
#include "utils_if.h"
#include "pin_mux_config.h"

/* SSD1351 Commands */
#define SSD1351_CMD_SETCOLUMN       0x15
#define SSD1351_CMD_SETROW          0x75
#define SSD1351_CMD_WRITERAM        0x5C
#define SSD1351_CMD_SETREMAP        0xA0
#define SSD1351_CMD_STARTLINE       0xA1
#define SSD1351_CMD_DISPLAYOFFSET   0xA2
#define SSD1351_CMD_NORMALDISPLAY   0xA6
#define SSD1351_CMD_DISPLAYON       0xAF
#define SSD1351_CMD_DISPLAYOFF      0xAE
#define SSD1351_CMD_CLOCKDIV        0xB3
#define SSD1351_CMD_COMMANDLOCK     0xFD
#define SSD1351_CMD_SETCONTRAST     0xC1
#define SSD1351_CMD_MASTERCURRENT   0xC7
#define SSD1351_CMD_PRECHARGE       0xB1
#define SSD1351_CMD_VCOMH           0xBE
#define SSD1351_CMD_CONTRASTABC     0xC1
#define SSD1351_CMD_CONTRASTMASTER  0xC7
#define SSD1351_CMD_SETVSL          0xB4
#define SSD1351_CMD_PRECHARGE2      0xB6

/* 16-bit RGB565 colour macros */
#define RGB565(r,g,b)  (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

#define OLED_COLOR_BLACK    RGB565(0,   0,   0  )
#define OLED_COLOR_WHITE    RGB565(255, 255, 255)
#define OLED_COLOR_CYAN     RGB565(0,   255, 255)
#define OLED_COLOR_GREEN    RGB565(0,   255, 0  )
#define OLED_COLOR_YELLOW   RGB565(255, 255, 0  )

/* Font 6x8 — minimal ASCII printable characters */
/* Each character is 5 columns wide, 8 pixels tall (LSB = top) */
static const uint8_t font6x8[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' 0x20 */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x08,0x14,0x54,0x54,0x3C}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
};

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */
static void _dc_cmd(void)
{
    GPIOPinWrite(OLED_DC_BASE, OLED_DC_PIN, 0);
}
static void _dc_data(void)
{
    GPIOPinWrite(OLED_DC_BASE, OLED_DC_PIN, OLED_DC_PIN);
}
static void _spi_write_byte(uint8_t byte)
{
    MAP_SPIDataPut(OLED_SPI_BASE, byte);
    uint32_t dummy; MAP_SPIDataGet(OLED_SPI_BASE, &dummy);
}
static void _cmd(uint8_t c)
{
    _dc_cmd();
    _spi_write_byte(c);
}
static void _data(uint8_t d)
{
    _dc_data();
    _spi_write_byte(d);
}

/* -----------------------------------------------------------------------
 * OLED_Init
 * ----------------------------------------------------------------------- */
void OLED_Init(void)
{
    /* Reset pulse */
    GPIOPinWrite(OLED_RST_BASE, OLED_RST_PIN, OLED_RST_PIN);
    MAP_UtilsDelay(8000000);
    GPIOPinWrite(OLED_RST_BASE, OLED_RST_PIN, 0);
    MAP_UtilsDelay(8000000);
    GPIOPinWrite(OLED_RST_BASE, OLED_RST_PIN, OLED_RST_PIN);
    MAP_UtilsDelay(8000000);

    /* SPI: 20 MHz, master, 8-bit, Mode 0 */
    MAP_SPIReset(OLED_SPI_BASE);
    MAP_SPIConfigSetExpClk(OLED_SPI_BASE,
                           MAP_PRCMPeripheralClockGet(PRCM_GSPI),
                           20000000,
                           SPI_MODE_MASTER,
                           SPI_SUB_MODE_0,
                           (SPI_SW_CTRL_CS | SPI_4PIN_MODE | SPI_TURBO_OFF |
                            SPI_CS_ACTIVEHIGH | SPI_WL_8));
    MAP_SPIEnable(OLED_SPI_BASE);

    /* Unlock command lock */
    _cmd(SSD1351_CMD_COMMANDLOCK); _data(0x12);
    _cmd(SSD1351_CMD_COMMANDLOCK); _data(0xB1);

    _cmd(SSD1351_CMD_DISPLAYOFF);

    _cmd(SSD1351_CMD_CLOCKDIV);   _data(0xF1);
    _cmd(SSD1351_CMD_SETCOLUMN);  _data(0x00); _data(0x7F);
    _cmd(SSD1351_CMD_SETROW);     _data(0x00); _data(0x7F);
    _cmd(SSD1351_CMD_SETREMAP);   _data(0x74); /* 65K, BGR, COM split */
    _cmd(SSD1351_CMD_STARTLINE);  _data(0x00);
    _cmd(SSD1351_CMD_DISPLAYOFFSET); _data(0x00);
    _cmd(SSD1351_CMD_NORMALDISPLAY);
    _cmd(SSD1351_CMD_CONTRASTABC);   _data(0xC8); _data(0x80); _data(0xC8);
    _cmd(SSD1351_CMD_CONTRASTMASTER);_data(0x0F);
    _cmd(SSD1351_CMD_SETVSL);        _data(0xA0); _data(0xB5); _data(0x55);
    _cmd(SSD1351_CMD_PRECHARGE);     _data(0x32);
    _cmd(SSD1351_CMD_PRECHARGE2);    _data(0x01);
    _cmd(SSD1351_CMD_VCOMH);         _data(0x05);

    _cmd(SSD1351_CMD_DISPLAYON);

    OLED_FillScreen(OLED_COLOR_BLACK);
}

/* -----------------------------------------------------------------------
 * OLED_SetWindow — set pixel write region
 * ----------------------------------------------------------------------- */
static void _set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    _cmd(SSD1351_CMD_SETCOLUMN);  _data(x0); _data(x1);
    _cmd(SSD1351_CMD_SETROW);     _data(y0); _data(y1);
    _cmd(SSD1351_CMD_WRITERAM);
    _dc_data();
}

/* -----------------------------------------------------------------------
 * OLED_FillScreen
 * ----------------------------------------------------------------------- */
void OLED_FillScreen(uint16_t color)
{
    uint8_t hi = color >> 8, lo = color & 0xFF;
    _set_window(0, 0, 127, 127);
    uint16_t i;
    for (i = 0; i < 128*128; i++)
    {
        _spi_write_byte(hi);
        _spi_write_byte(lo);
    }
}

/* -----------------------------------------------------------------------
 * OLED_DrawChar — 6x8 character at pixel (x, y)
 * ----------------------------------------------------------------------- */
void OLED_DrawChar(uint8_t x, uint8_t y, char c, uint16_t fg, uint16_t bg)
{
    if (c < 0x20 || c > 0x7A) c = '?';
    const uint8_t *glyph = font6x8[c - 0x20];
    uint8_t col, row;
    uint8_t fg_hi = fg >> 8, fg_lo = fg & 0xFF;
    uint8_t bg_hi = bg >> 8, bg_lo = bg & 0xFF;

    _set_window(x, y, x+5, y+7);
    for (row = 0; row < 8; row++)
    {
        for (col = 0; col < 5; col++)
        {
            if (glyph[col] & (1 << row))
            {
                _spi_write_byte(fg_hi); _spi_write_byte(fg_lo);
            }
            else
            {
                _spi_write_byte(bg_hi); _spi_write_byte(bg_lo);
            }
        }
        /* 6th pixel always background */
        _spi_write_byte(bg_hi); _spi_write_byte(bg_lo);
    }
}

/* -----------------------------------------------------------------------
 * OLED_DrawString
 * ----------------------------------------------------------------------- */
void OLED_DrawString(uint8_t x, uint8_t y, const char *str,
                     uint16_t fg, uint16_t bg)
{
    while (*str && x < 122)
    {
        OLED_DrawChar(x, y, *str++, fg, bg);
        x += 6;
    }
}

/* -----------------------------------------------------------------------
 * OLED_Printf  — convenience wrapper
 * ----------------------------------------------------------------------- */
void OLED_Printf(uint8_t x, uint8_t y, uint16_t fg, uint16_t bg,
                 const char *fmt, ...)
{
    char buf[22];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OLED_DrawString(x, y, buf, fg, bg);
}

/* -----------------------------------------------------------------------
 * OLED_DrawRect — filled rectangle
 * ----------------------------------------------------------------------- */
void OLED_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
    uint8_t hi = color >> 8, lo = color & 0xFF;
    _set_window(x, y, x+w-1, y+h-1);
    uint16_t i;
    for (i = 0; i < (uint16_t)w * h; i++)
    {
        _spi_write_byte(hi); _spi_write_byte(lo);
    }
}
