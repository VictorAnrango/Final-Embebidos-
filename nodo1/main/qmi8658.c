#include "qmi8658.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "QMI8658";

#define REG_CTRL1   0x02
#define REG_CTRL2   0x03
#define REG_CTRL3   0x04
#define REG_CTRL7   0x08
#define REG_AX_L    0x35
#define REG_RESET   0x60

static esp_err_t i2c_write_reg(uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(QMI8658_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_regs(uint8_t reg, uint8_t *buf, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (QMI8658_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1)
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(QMI8658_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t qmi8658_init(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = QMI8658_SDA_PIN,
        .scl_io_num       = QMI8658_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t ret = i2c_param_config(QMI8658_I2C_PORT, &conf);
    if (ret != ESP_OK) return ret;

    ret = i2c_driver_install(QMI8658_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_write_reg(REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));
    i2c_write_reg(REG_CTRL1, 0x60);
    i2c_write_reg(REG_CTRL2, 0x95);
    i2c_write_reg(REG_CTRL3, 0xD5);
    i2c_write_reg(REG_CTRL7, 0x03);

    ESP_LOGI(TAG, "QMI8658 OK");
    return ESP_OK;
}

esp_err_t qmi8658_read(qmi8658_data_t *data) {
    uint8_t buf[12];
    esp_err_t ret = i2c_read_regs(REG_AX_L, buf, 12);
    if (ret != ESP_OK) return ret;

    int16_t ax = (int16_t)(buf[1]  << 8 | buf[0]);
    int16_t ay = (int16_t)(buf[3]  << 8 | buf[2]);
    int16_t az = (int16_t)(buf[5]  << 8 | buf[4]);
    int16_t gx = (int16_t)(buf[7]  << 8 | buf[6]);
    int16_t gy = (int16_t)(buf[9]  << 8 | buf[8]);
    int16_t gz = (int16_t)(buf[11] << 8 | buf[10]);

    data->acc_x  = ax / 4096.0f;
    data->acc_y  = ay / 4096.0f;
    data->acc_z  = az / 4096.0f;
    data->gyro_x = gx / 64.0f;
    data->gyro_y = gy / 64.0f;
    data->gyro_z = gz / 64.0f;
    return ESP_OK;
}