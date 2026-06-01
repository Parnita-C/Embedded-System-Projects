//*****************************************************************************
//
// oled_ssd1351.h
//
// SSD1351 SPI OLED driver header for CC3200.
//
//*****************************************************************************

#ifndef OLED_SSD1351_H_
#define OLED_SSD1351_H_

// RGB565 colors
#define COLOR_BLACK                  0x0000
#define COLOR_WHITE                  0xFFFF
#define COLOR_RED                    0xF800
#define COLOR_GREEN                  0x07E0
#define COLOR_BLUE                   0x001F
#define COLOR_YELLOW                 0xFFE0
#define COLOR_CYAN                   0x07FF
#define COLOR_MAGENTA                0xF81F

void OLED_PinMuxConfig(void);
void OLED_SPI_Init(void);
void OLED_Init(void);

void OLED_ShowStartupScreen(void);
void OLED_ShowCalibrationScreen(unsigned int current, unsigned int total);
void OLED_ShowStepCount(unsigned long steps);
void OLED_ShowError(const char *line1, const char *line2);
void OLED_ShowHalloWorld(void);
void OLED_ShowStepMode(unsigned long steps);
void OLED_ShowPushupMode(unsigned long pushups);

#endif
