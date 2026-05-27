#include "mpu6050.h"
#include "shared_data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

#define TAG "MPU6050"

#define I2C_MASTER_NUM       I2C_NUM_0
#define MPU6050_ADDR         0x68
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B

void mpu6050_init(void)
{
    uint8_t data[2] = {MPU6050_PWR_MGMT_1, 0x00};
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_MASTER_NUM,
                    MPU6050_ADDR, data, 2, pdMS_TO_TICKS(1000)));
    ESP_LOGI(TAG, "MPU6050 iniciado");
}

void task_mpu(void *pvParameters)
{
    uint8_t reg = MPU6050_ACCEL_XOUT_H;
    uint8_t buf[14];

    while (1)
    {
        esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM,
                            MPU6050_ADDR, &reg, 1, buf, 14,
                            pdMS_TO_TICKS(1000));

        if (err == ESP_OK) {
            float ax = (int16_t)((buf[0]  << 8) | buf[1])  / 16384.0f;
            float ay = (int16_t)((buf[2]  << 8) | buf[3])  / 16384.0f;
            float az = (int16_t)((buf[4]  << 8) | buf[5])  / 16384.0f;
            float gx = (int16_t)((buf[8]  << 8) | buf[9])  / 131.0f;
            float gy = (int16_t)((buf[10] << 8) | buf[11]) / 131.0f;
            float gz = (int16_t)((buf[12] << 8) | buf[13]) / 131.0f;

            shared_data_lock();
            shared_data.acc_x  = ax;
            shared_data.acc_y  = ay;
            shared_data.acc_z  = az;
            shared_data.gyro_x = gx;
            shared_data.gyro_y = gy;
            shared_data.gyro_z = gz;
            shared_data_unlock();
        } else {
            ESP_LOGE(TAG, "Error leyendo MPU6050");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}