#include "leds.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_BLANCO_GPIO 25
#define LED_ROJO_GPIO   26

static const char *TAG = "LEDS";

void leds_init(void)
{
    gpio_reset_pin(LED_BLANCO_GPIO);
    gpio_reset_pin(LED_ROJO_GPIO);

    gpio_set_direction(LED_BLANCO_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ROJO_GPIO,   GPIO_MODE_OUTPUT);

    gpio_set_level(LED_BLANCO_GPIO, 1);
    gpio_set_level(LED_ROJO_GPIO,   0);

    ESP_LOGI(TAG, "LEDs iniciados");
}

void leds_set_alerta(bool hay_alerta)
{
    gpio_set_level(LED_ROJO_GPIO,   hay_alerta ? 1 : 0);
    gpio_set_level(LED_BLANCO_GPIO, hay_alerta ? 0 : 1);
}