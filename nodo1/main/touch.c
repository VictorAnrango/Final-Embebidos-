#include "touch.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TOUCH";

#define REG_GESTURE   0x01
#define REG_TOUCH_NUM 0x02
#define REG_XH        0x03
#define REG_XL        0x04
#define REG_YH        0x05
#define REG_YL        0x06

static esp_err_t touch_i2c_read(uint8_t reg, uint8_t *buf, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TOUCH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TOUCH_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1)
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(TOUCH_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t touch_init(void) {
    gpio_config_t rst_io = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_io);

    gpio_config_t int_io = {
        .pin_bit_mask = (1ULL << TOUCH_INT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&int_io);

    gpio_set_level(TOUCH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "CST816S OK | INT=GPIO%d RST=GPIO%d",
             TOUCH_INT_PIN, TOUCH_RST_PIN);
    return ESP_OK;
}

touch_data_t touch_read(void) {
    touch_data_t td = {0};
    uint8_t buf[6];

    if (touch_i2c_read(REG_GESTURE, buf, 6) != ESP_OK) {
        return td;
    }

    td.gesture = (touch_gesture_t)buf[0];
    td.touched = buf[1] & 0x0F;

    if (td.touched > 0) {
        td.x = ((buf[2] & 0x0F) << 8) | buf[3];
        td.y = ((buf[4] & 0x0F) << 8) | buf[5];
    }
    return td;
}