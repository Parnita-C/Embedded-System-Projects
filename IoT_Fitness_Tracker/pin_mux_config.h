/*
 * pin_mux_config.h
 *
 * Smart IoT Fitness Trainer - CC3200 LaunchXL
 */

#ifndef PIN_MUX_CONFIG_H_
#define PIN_MUX_CONFIG_H_

/* -----------------------------------------------------------------------
 * GPIO Base / Bit mappings (CC3200 SDK convention)
 *   GPIO 0-7 GPIOA0_BASE,  bit = (1 << (gpio_num % 8))
 *   GPIO 8-15   GPIOA1_BASE
 *   GPIO 16-23  GPIOA2_BASE
 *   GPIO 24-31  GPIOA3_BASE
 * ----------------------------------------------------------------------- */

/* OLED control */
#define OLED_DC_BASE        GPIOA0_BASE
#define OLED_DC_PIN         GPIO_PIN_6    /* GPIO 6,  physical Pin 61 */
#define OLED_RST_BASE       GPIOA0_BASE
#define OLED_RST_PIN        GPIO_PIN_7    /* GPIO 7,  physical Pin 62 */

/* IR Sensor */
#define IR_SENSOR_BASE      GPIOA2_BASE
#define IR_SENSOR_PIN       GPIO_PIN_6    /* GPIO 22, physical Pin 15 */

/* Push Buttons */
#define SW2_BASE            GPIOA1_BASE
#define SW2_PIN             GPIO_PIN_5    /* GPIO 13, physical Pin 4  */
#define SW3_BASE            GPIOA2_BASE
#define SW3_PIN             GPIO_PIN_1    /* GPIO 17, physical Pin 16 */

/* SPI GSPI base (OLED) */
#define OLED_SPI_BASE       GSPI_BASE

/* I2C base (Accelerometer) */
#define ACCEL_I2C_BASE      I2CA0_BASE
#define BMA222_I2C_ADDR     0x18          /* BMA222 default address */

extern void PinMuxConfig(void);

#endif /* PIN_MUX_CONFIG_H_ */
