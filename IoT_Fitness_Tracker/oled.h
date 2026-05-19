/*
 * oled.h
 *
 * SSD1351 OLED Driver — CC3200 Smart Fitness Trainer
 */

#ifndef OLED_H_
#define OLED_H_

#include <stdint.h>

/* Common colours (RGB565) */
#define OLED_BLACK    0x0000
#define OLED_WHITE    0xFFFF
#define OLED_CYAN     0x07FF
#define OLED_GREEN    0x07E0
#define OLED_YELLOW   0xFFE0
#define OLED_RED      0xF800
#define OLED_BLUE     0x001F
#define OLED_GRAY     0x7BEF

void OLED_Init(void);
void OLED_FillScreen(uint16_t color);
void OLED_DrawChar(uint8_t x, uint8_t y, char c, uint16_t fg, uint16_t bg);
void OLED_DrawString(uint8_t x, uint8_t y, const char *str,
                     uint16_t fg, uint16_t bg);
void OLED_Printf(uint8_t x, uint8_t y, uint16_t fg, uint16_t bg,
                 const char *fmt, ...);
void OLED_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);

#endif /* OLED_H_ */
