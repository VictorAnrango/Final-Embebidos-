#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include "qmi8658.h"

#define MAX30100_I2C_ADDR   0x57
#define MAX30100_I2C_PORT   I2C_NUM_1
#define MAX30100_SDA_PIN    16
#define MAX30100_SCL_PIN    17

typedef struct {
    int bpm;
    int spo2;
    int valid;
} pulse_data_t;

esp_err_t     pulse_sensor_init(void);
void          pulse_sensor_sample(void);
pulse_data_t  pulse_sensor_get(void);