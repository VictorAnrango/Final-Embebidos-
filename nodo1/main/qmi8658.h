#pragma once
#include "driver/i2c.h"
#include "esp_err.h"

#define QMI8658_I2C_ADDR    0x6B
#define QMI8658_I2C_PORT    I2C_NUM_0
#define QMI8658_SDA_PIN     6
#define QMI8658_SCL_PIN     7

typedef struct {
    float acc_x,  acc_y,  acc_z;
    float gyro_x, gyro_y, gyro_z;
} qmi8658_data_t;

esp_err_t qmi8658_init(void);
esp_err_t qmi8658_read(qmi8658_data_t *data);