#include "wifi_espnow.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "shared_types.h"
#include "alerts.h"

// ============================================================
// Configuración WiFi
// ============================================================

#define WIFI_SSID "VILLA_MARIA"
#define WIFI_PASS "12345678"

#define WIFI_CONNECTED_BIT BIT0

static const char         *TAG             = "WIFI_ESPNOW";
static EventGroupHandle_t  wifi_event_group = NULL;

// Mutex para proteger escrituras en datos_nodo1 / datos_nodo2
SemaphoreHandle_t datos_mutex = NULL;

// ============================================================
// Datos globales (declarados en shared_types.h como extern,
// definidos aquí una única vez)
// ============================================================

nodo1_data_t datos_nodo1  = {0};
nodo2_data_t datos_nodo2  = {0};
bool         nodo1_recibido = false;
bool         nodo2_recibido = false;

// ============================================================
// WiFi event handler
// ============================================================

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d =
            (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi desconectado reason=%d, reconectando...", d->reason);
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ============================================================
// ESP-NOW callback
// ============================================================

static void on_data_recv(const esp_now_recv_info_t *recv_info,
                         const uint8_t *data,
                         int len)
{
    ESP_LOGI(TAG, "Paquete raw: len=%d | sizeof(nodo1)=%d | sizeof(nodo2)=%d",
             len,
             (int)sizeof(nodo1_data_t),
             (int)sizeof(nodo2_data_t));
             
    if (len < (int)sizeof(int))
    {
        ESP_LOGW(TAG, "Paquete demasiado pequeño (%d bytes)", len);
        return;
    }

    int node_id;
    memcpy(&node_id, data, sizeof(int));

    ESP_LOGI(TAG, "Paquete recibido: node_id=%d len=%d | esperado nodo1=%d nodo2=%d",
         node_id, len,
         (int)sizeof(nodo1_data_t),
         (int)sizeof(nodo2_data_t));

    if (node_id == 1)
    {
        if (len != (int)sizeof(nodo1_data_t))
        {
            ESP_LOGW(TAG, "Nodo1 tamaño incorrecto: %d (esperado %d)",
                     len, (int)sizeof(nodo1_data_t));
            return;
        }

        if (xSemaphoreTake(datos_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            memcpy(&datos_nodo1, data, sizeof(nodo1_data_t));
            nodo1_recibido = true;
            xSemaphoreGive(datos_mutex);
        }

        ESP_LOGI(TAG, "Nodo1 → BPM=%d SpO2=%.1f",
                 datos_nodo1.bpm, datos_nodo1.spo2);
    }
    else if (node_id == 2)
    {
        if (len != (int)sizeof(nodo2_data_t))
        {
            ESP_LOGW(TAG, "Nodo2 tamaño incorrecto: %d (esperado %d)",
                     len, (int)sizeof(nodo2_data_t));
            return;
        }

        if (xSemaphoreTake(datos_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            memcpy(&datos_nodo2, data, sizeof(nodo2_data_t));
            nodo2_recibido = true;
            xSemaphoreGive(datos_mutex);
        }

        ESP_LOGI(TAG, "Nodo2 → LAT=%.4f VEL=%.1f",
                 datos_nodo2.latitud, datos_nodo2.velocidad);
    }
    else
    {
        ESP_LOGW(TAG, "Nodo desconocido: %d", node_id);
        return;
    }

    // Evaluar alertas cada vez que llegue un dato válido
    alerts_evaluate();
}

// ============================================================
// Inicialización
// ============================================================

void wifi_espnow_init(void)
{
    datos_mutex      = xSemaphoreCreateMutex();
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid,     WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Esperando conexión WiFi...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    ESP_LOGI(TAG, "WiFi conectado, canal=%d", primary);

    // --- ESP-NOW ---
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    ESP_LOGI(TAG, "ESP-NOW iniciado");
}