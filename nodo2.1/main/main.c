#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "ssd1306.h"

#include "shared_data.h"
#include "mpu6050.h"
#include "gps.h"
#include "oled.h"
#include "espnow.h"

#define TAG "MAIN"

#define I2C_MASTER_SCL_IO  22
#define I2C_MASTER_SDA_IO  21
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define OLED_ADDR          0x3C

static void i2c_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM,
                                       conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "I2C iniciado");
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi iniciado");
}

void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Mutex y datos iniciales
    data_mutex = xSemaphoreCreateMutex();
    memset(&shared_data, 0, sizeof(shared_data));
    shared_data.node_id = 2;

    // Hardware base
    i2c_init();
    mpu6050_init();
    gps_init();

    // OLED
    ssd1306_handle_t oled = ssd1306_create(I2C_MASTER_NUM, OLED_ADDR);
    if (oled == NULL) {
        ESP_LOGE(TAG, "Error creando OLED");
        return;
    }
    ESP_ERROR_CHECK(ssd1306_init(oled));
    ssd1306_clear_screen(oled, false);
    ssd1306_refresh_gram(oled);
    oled_init(oled);

    // WiFi + ESPNOW
    wifi_init();
    espnow_init();

    // Tareas
    xTaskCreate(task_mpu,  "mpu",  2048, NULL, 3, NULL);
    xTaskCreate(task_gps,  "gps",  3072, NULL, 3, NULL);
    xTaskCreate(task_oled, "oled", 2048, NULL, 2, NULL);
    xTaskCreate(task_send, "send", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "Sistema iniciado — 4 tareas corriendo");
}