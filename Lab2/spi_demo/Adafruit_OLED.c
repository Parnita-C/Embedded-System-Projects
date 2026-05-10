
// Standard includes
#include <string.h>

// Driverlib includes
#include "hw_types.h"
#include "hw_memmap.h"
#include "hw_common_reg.h"
#include "hw_ints.h"
#include "gpio.h"
#include "spi.h"
#include "rom.h"
#include "rom_map.h"
#include "utils.h"
#include "prcm.h"
#include "uart.h"
#include "interrupt.h"
#include "glcdfont.h"

// Common interface includes
#include "uart_if.h"
#include "pin_mux_config.h"

#include "Adafruit_SSD1351.h"

#define SPI_IF_BIT_RATE  100000


#define OLED_CS_BASE  GPIOA0_BASE   // Dev Pin 16
#define OLED_CS_PIN   GPIO_PIN_4

#define OLED_DC_BASE  GPIOA0_BASE   // Dev Pin 18
#define OLED_DC_PIN   GPIO_PIN_6

#define OLED_RST_BASE  GPIOA3_BASE
#define OLED_RST_PIN   GPIO_PIN_2
//*****************************************************************************

void writeCommand(unsigned char c)
{
    unsigned long ulDummy;

    // set CS to low to starting reading data -> Pin 61
    MAP_SPICSEnable(GSPI_BASE);
    GPIOPinWrite(GPIOA0_BASE, 0x40, 0x00);

    // set DC to low to write command -> Pin 62
    GPIOPinWrite(GPIOA0_BASE, 0x80, 0x00);

    // send the character
    MAP_SPIDataPut(GSPI_BASE, c);

    // clean up the received register
    MAP_SPIDataGet(GSPI_BASE, &ulDummy);

    // set CS to high to end sending data -> Pin 61

    MAP_SPICSDisable(GSPI_BASE);
    GPIOPinWrite(GPIOA0_BASE, 0x40, 0x40);

}

//*****************************************************************************

void writeData(unsigned char c)
{
    unsigned long ulDummy;

    // set CS to low to starting reading data -> Pin 61
    MAP_SPICSEnable(GSPI_BASE);
    GPIOPinWrite(GPIOA0_BASE, 0x40, 0x00);

    // set DC to high to write data -> Pin 62
    GPIOPinWrite(GPIOA0_BASE, 0x80, 0x80);

    // send the data
    MAP_SPIDataPut(GSPI_BASE, c);

    // clean up received register
    MAP_SPIDataGet(GSPI_BASE, &ulDummy);

    // set CS to high to end sending data -> Pin 61
    MAP_SPICSDisable(GSPI_BASE);
    GPIOPinWrite(GPIOA0_BASE, 0x40, 0x40);
}

//*****************************************************************************
void Adafruit_Init(void){

//TODO 3
/* NOTE: This function assumes that the RESET pin of the 
*  OLED has been wired to GPIO28, pin 18 (P2.2). If you 
*  use a different pin for the OLED reset, then you should
*  update the GPIOPinWrite commands below that set RESET 
*  high or low.
*/
  volatile unsigned long delay;

  GPIOPinWrite(OLED_RST_BASE, OLED_RST_PIN, 0);   // RESET low

  for(delay=0; delay<100; delay=delay+1);// delay minimum 100 ns

  GPIOPinWrite(OLED_RST_BASE, OLED_RST_PIN, OLED_RST_PIN);	// RESET = RESET_HIGH

	// Initialization Sequence
  writeCommand(SSD1351_CMD_COMMANDLOCK);  // set command lock
  writeData(0x12);
  writeCommand(SSD1351_CMD_COMMANDLOCK);  // set command lock
  writeData(0xB1);

  writeCommand(SSD1351_CMD_DISPLAYOFF);  		// 0xAE

  writeCommand(SSD1351_CMD_CLOCKDIV);  		// 0xB3
  writeCommand(0xF1);  						// 7:4 = Oscillator Frequency, 3:0 = CLK Div Ratio (A[3:0]+1 = 1..16)

  writeCommand(SSD1351_CMD_MUXRATIO);
  writeData(127);

  writeCommand(SSD1351_CMD_SETREMAP);
  writeData(0x74);

  writeCommand(SSD1351_CMD_SETCOLUMN);
  writeData(0x00);
  writeData(0x7F);
  writeCommand(SSD1351_CMD_SETROW);
  writeData(0x00);
  writeData(0x7F);

  writeCommand(SSD1351_CMD_STARTLINE); 		// 0xA1
  if (SSD1351HEIGHT == 96) {
    writeData(96);
  } else {
    writeData(0);
  }


  writeCommand(SSD1351_CMD_DISPLAYOFFSET); 	// 0xA2
  writeData(0x0);

  writeCommand(SSD1351_CMD_SETGPIO);
  writeData(0x00);

  writeCommand(SSD1351_CMD_FUNCTIONSELECT);
  writeData(0x01); // internal (diode drop)
  //writeData(0x01); // external bias

//    writeCommand(SSSD1351_CMD_SETPHASELENGTH);
//    writeData(0x32);

  writeCommand(SSD1351_CMD_PRECHARGE);  		// 0xB1
  writeCommand(0x32);

  writeCommand(SSD1351_CMD_VCOMH);  			// 0xBE
  writeCommand(0x05);

  writeCommand(SSD1351_CMD_NORMALDISPLAY);  	// 0xA6

  writeCommand(SSD1351_CMD_CONTRASTABC);
  writeData(0xC8);
  writeData(0x80);
  writeData(0xC8);

  writeCommand(SSD1351_CMD_CONTRASTMASTER);
  writeData(0x0F);

  writeCommand(SSD1351_CMD_SETVSL );
  writeData(0xA0);
  writeData(0xB5);
  writeData(0x55);

  writeCommand(SSD1351_CMD_PRECHARGE2);
  writeData(0x01);

  writeCommand(SSD1351_CMD_DISPLAYON);		//--turn on oled panel
}

/***********************************/

void goTo(int x, int y) {
  if ((x >= SSD1351WIDTH) || (y >= SSD1351HEIGHT)) return;

  // set x and y coordinate
  writeCommand(SSD1351_CMD_SETCOLUMN);
  writeData(x);
  writeData(SSD1351WIDTH-1);

  writeCommand(SSD1351_CMD_SETROW);
  writeData(y);
  writeData(SSD1351HEIGHT-1);

  writeCommand(SSD1351_CMD_WRITERAM);
}

unsigned int Color565(unsigned char r, unsigned char g, unsigned char b) {
  unsigned int c;
  c = r >> 3;
  c <<= 6;
  c |= g >> 2;
  c <<= 5;
  c |= b >> 3;

  return c;
}

void fillScreen(unsigned int fillcolor) {
  fillRect(0, 0, SSD1351WIDTH, SSD1351HEIGHT, fillcolor);
}

/**************************************************************************/
/*!
    @brief  Draws a filled rectangle using HW acceleration
*/
/**************************************************************************/
void fillRect(unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int fillcolor)
{
  unsigned int i;

  // Bounds check
  if ((x >= SSD1351WIDTH) || (y >= SSD1351HEIGHT))
	return;

  // Y bounds check
  if (y+h > SSD1351HEIGHT)
  {
    h = SSD1351HEIGHT - y - 1;
  }

  // X bounds check
  if (x+w > SSD1351WIDTH)
  {
    w = SSD1351WIDTH - x - 1;
  }

  // set location
  writeCommand(SSD1351_CMD_SETCOLUMN);
  writeData(x);
  writeData(x+w-1);
  writeCommand(SSD1351_CMD_SETROW);
  writeData(y);
  writeData(y+h-1);
  // fill!
  writeCommand(SSD1351_CMD_WRITERAM);

  for (i=0; i < w*h; i++) {
    writeData(fillcolor >> 8);
    writeData(fillcolor);
  }
}

void drawFastVLine(int x, int y, int h, unsigned int color) {

  unsigned int i;
  // Bounds check
  if ((x >= SSD1351WIDTH) || (y >= SSD1351HEIGHT))
	return;

  // X bounds check
  if (y+h > SSD1351HEIGHT)
  {
    h = SSD1351HEIGHT - y - 1;
  }

  if (h < 0) return;

  // set location
  writeCommand(SSD1351_CMD_SETCOLUMN);
  writeData(x);
  writeData(x);
  writeCommand(SSD1351_CMD_SETROW);
  writeData(y);
  writeData(y+h-1);
  // fill!
  writeCommand(SSD1351_CMD_WRITERAM);

  for (i=0; i < h; i++) {
    writeData(color >> 8);
    writeData(color);
  }
}



void drawFastHLine(int x, int y, int w, unsigned int color) {

  unsigned int i;
  // Bounds check
  if ((x >= SSD1351WIDTH) || (y >= SSD1351HEIGHT))
	return;

  // X bounds check
  if (x+w > SSD1351WIDTH)
  {
    w = SSD1351WIDTH - x - 1;
  }

  if (w < 0) return;

  // set location
  writeCommand(SSD1351_CMD_SETCOLUMN);
  writeData(x);
  writeData(x+w-1);
  writeCommand(SSD1351_CMD_SETROW);
  writeData(y);
  writeData(y);
  // fill!
  writeCommand(SSD1351_CMD_WRITERAM);

  for (i=0; i < w; i++) {
    writeData(color >> 8);
    writeData(color);
  }
}




void drawPixel(int x, int y, unsigned int color)
{
  if ((x >= SSD1351WIDTH) || (y >= SSD1351HEIGHT)) return;
  if ((x < 0) || (y < 0)) return;

  goTo(x, y);

  writeData(color >> 8);
  writeData(color);
}


void  invert(char v) {
   if (v) {
     writeCommand(SSD1351_CMD_INVERTDISPLAY);
   } else {
     	writeCommand(SSD1351_CMD_NORMALDISPLAY);
   }
 }

void drawChar5x7(int x, int y, unsigned char c,
                 unsigned int fg,
                 unsigned int bg)
{
    unsigned char colBits;
    int col, row;

    for (col = 0; col < 5; col++) {
        colBits = font[(unsigned int)c * 5 + col];

        for (row = 0; row < 8; row++) {
            if (colBits & 0x01) {
                drawPixel(x + col, y + row, fg);
            } else {
                drawPixel(x + col, y + row, bg);
            }
            colBits >>= 1;
        }
    }
}

void drawString5x7(int x, int y, char *s,
                   unsigned int fg,
                   unsigned int bg)
{
    while (*s) {
        drawChar5x7(x, y, (unsigned char)*s, fg, bg);
        x += 6;   // 5 pixels + 1 spacing
        s++;
    }
}

void drawFullCharset(void)
{
    int x = 0;
    int y = 0;
    unsigned char c;

    fillScreen(Color565(0, 0, 0));   // BLACK

    for (c = 0x00; c <= 0xFF; c++) {
        drawChar5x7(x, y, c,
                    Color565(255,255,255),  // WHITE
                    Color565(0,0,0));       // BLACK

        x += 6;

        if (x + 6 >= SSD1351WIDTH) {
            x = 0;
            y += 8;

            if (y + 8 >= SSD1351HEIGHT)
                break;
        }
    }
}



