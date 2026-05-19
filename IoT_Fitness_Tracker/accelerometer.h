/*
 * accelerometer.h
 *
 * BMA222 Accelerometer Driver — CC3200 Smart Fitness Trainer
 */

#ifndef ACCELEROMETER_H_
#define ACCELEROMETER_H_

#include <stdint.h>
#include <stdbool.h>
#include "pin_mux_config.h"

typedef struct {
    int8_t  x, y, z;          /* raw 8-bit signed counts */
    int16_t x_mg, y_mg, z_mg; /* converted to milli-g    */
} AccelData_t;

bool     Accel_Init(void);
bool     Accel_ReadRaw(AccelData_t *pData);
bool     Accel_ProcessStep(uint32_t current_tick_ms);
uint32_t Accel_GetStepCount(void);
void     Accel_ResetStepCount(void);

#endif /* ACCELEROMETER_H_ */
