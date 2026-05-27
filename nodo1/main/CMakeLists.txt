idf_component_register(
    SRCS
        "main.c"
        "qmi8658.c"
        "pulse_sensor.c"
        "display.c"
        "touch.c"
        "espnow_send.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_driver_gpio
        esp_driver_spi
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        freertos
        log
        esp_timer
        esp_adc
        spi_flash
)

target_link_libraries(${COMPONENT_LIB} PUBLIC m)