/*
 * pin_mux_config.h
 *
 * Smart IoT Fitness Trainer — CC3200 LaunchXL
 * Pin definitions for all peripherals.
 *
 * GPIO base / bit mapping (CC3200 SDK convention):
 *   GPIO  0-7  → GPIOA0_BASE,  bit = (1 << (gpio % 8))
 *   GPIO  8-15 → GPIOA1_BASE
 *   GPIO 16-23 → GPIOA2_BASE
 *   GPIO 24-31 → GPIOA3_BASE
 */

#ifndef PIN_MUX_CONFIG_H_
#define PIN_MUX_CONFIG_H_

/* -----------------------------------------------------------------------
 * OLED control lines (SSD1351)
 *   D/C   → GPIO 6, physical Pin 61 (P1 header)
 *   RESET → GPIO 7, physical Pin 62 (P1 header)
 * ----------------------------------------------------------------------- */
#define OLED_DC_BASE    GPIOA0_BASE
#define OLED_DC_PIN     GPIO_PIN_6
#define OLED_RST_BASE   GPIOA0_BASE
#define OLED_RST_PIN    GPIO_PIN_7

/* -----------------------------------------------------------------------
 * IR Proximity Sensor
 *   OUT → GPIO 22, physical Pin 15 (P2 header), active LOW
 * ----------------------------------------------------------------------- */
#define IR_SENSOR_BASE  GPIOA2_BASE
#define IR_SENSOR_PIN   GPIO_PIN_6

/* -----------------------------------------------------------------------
 * Push Buttons (active LOW, internal pull-up)
 *   SW2 → GPIO 13, physical Pin 4  (P1 header)  — mode cycle / stop
 *   SW3 → GPIO 17, physical Pin 16 (P2 header)  — start / confirm
 * ----------------------------------------------------------------------- */
#define SW2_BASE        GPIOA1_BASE
#define SW2_PIN         GPIO_PIN_5    /* GPIO 13: bit 5 in GPIOA1 */
#define SW3_BASE        GPIOA2_BASE
#define SW3_PIN         GPIO_PIN_1    /* GPIO 17: bit 1 in GPIOA2 */

/* -----------------------------------------------------------------------
 * SPI bus (GSPI) — OLED display
 * ----------------------------------------------------------------------- */
#define OLED_SPI_BASE   GSPI_BASE

/* -----------------------------------------------------------------------
 * I2C bus (I2CA0) — BMA222 accelerometer
 * ----------------------------------------------------------------------- */
#define ACCEL_I2C_BASE  I2CA0_BASE
#define BMA222_I2C_ADDR 0x18          /* BMA222 default I2C address */

extern void PinMuxConfig(void);

#endif /* PIN_MUX_CONFIG_H_ */
