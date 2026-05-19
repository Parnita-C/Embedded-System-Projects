/*
 * ir_sensor.h
 *
 * IR Proximity Sensor Driver — CC3200 Smart Fitness Trainer
 */

#ifndef IR_SENSOR_H_
#define IR_SENSOR_H_

#include <stdint.h>
#include <stdbool.h>

void     IR_Init(void);
bool     IR_Read(void);
bool     IR_ProcessRep(void);
uint32_t IR_GetRepCount(void);
void     IR_ResetRepCount(void);

#endif /* IR_SENSOR_H_ */
