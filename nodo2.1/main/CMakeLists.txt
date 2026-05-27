idf_component_register(
    SRCS
        "main.c"
        "shared_data.c"
        "mpu6050.c"
        "gps.c"
        "oled.c"
        "espnow.c"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__ssd1306
        esp_driver_uart
        esp_driver_i2c
        esp_wifi
        nvs_flash
        esp_event
        esp_netif
)