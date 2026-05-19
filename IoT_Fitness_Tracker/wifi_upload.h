/*
 * wifi_upload.h
 *
 * Wi-Fi Upload Module — CC3200 Smart Fitness Trainer
 */

#ifndef WIFI_UPLOAD_H_
#define WIFI_UPLOAD_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MODE_PUSHUP  = 0,
    MODE_RUNNING = 1
} WorkoutMode_t;

bool WiFi_Connect(void);
bool WiFi_UploadWorkout(WorkoutMode_t mode, uint32_t count);
bool WiFi_IsConnected(void);

#endif /* WIFI_UPLOAD_H_ */
