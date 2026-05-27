#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"

// Módulos del proyecto
#include "shared_types.h"
#include "leds.h"
#include "oled.h"
#include "wifi_espnow.h"
#include "alerts.h"
#include "tasks.h"

static const char *TAG = "MAIN";

// ============================================================
// app_main
// Solo inicializa módulos en orden y lanza las tareas.
// Toda la lógica vive en los módulos correspondientes.
// ============================================================

void app_main(void)
{
    // --- NVS (requerido por WiFi) ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== Nodo 3 arrancando ===");

    // --- Periféricos físicos ---
    leds_init();
    oled_init();

    // --- Alertas (debe ir antes de wifi_espnow_init para que el
    //     callback ESPNOW pueda llamar a alerts_evaluate()) ---
    alerts_init();

    // --- WiFi + ESP-NOW (bloquea hasta obtener IP) ---
    wifi_espnow_init();

    // --- Log de MAC para registrar pares ESPNOW ---
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // --- Tareas FreeRTOS ---
    tasks_start();

    ESP_LOGI(TAG, "=== Nodo 3 listo ===");

    // app_main puede terminar; las tareas siguen corriendo
}