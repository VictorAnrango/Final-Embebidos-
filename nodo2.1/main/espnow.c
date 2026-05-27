#include "espnow.h"
#include "shared_data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include <string.h>

#define TAG       "ESPNOW"
#define WIFI_SSID "VILLA_MARIA"

static uint8_t receiver_mac[] = { 0x08, 0x3A, 0xF2, 0xB7, 0x2F, 0x38 };

static void on_data_sent(const wifi_tx_info_t *tx_info,
                         esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS)
        ESP_LOGI(TAG, "Enviado OK");
    else
        ESP_LOGW(TAG, "Error al enviar");
}

static uint8_t obtener_canal_wifi(const char *ssid_objetivo)
{
    uint16_t         ap_count = 0;
    wifi_ap_record_t ap_info[20];

    wifi_scan_config_t scan_config = {
        .ssid = NULL, .bssid = NULL,
        .channel = 0, .show_hidden = true
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    if (ap_count > 20) ap_count = 20;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_info));

    for (int i = 0; i < ap_count; i++) {
        if (strcmp((char *)ap_info[i].ssid, ssid_objetivo) == 0)
            return ap_info[i].primary;
    }
    ESP_LOGW(TAG, "Red no encontrada. Canal 1");
    return 1;
}

void espnow_init(void)
{
    uint8_t canal = obtener_canal_wifi(WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE));
    ESP_LOGI(TAG, "Canal: %d", canal);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    ESP_LOGI(TAG, "ESPNOW iniciado");
}

void task_send(void *pvParameters)
{
    while (1)
    {
        nodo2_data_t snap;

        shared_data_lock();
        snap = shared_data;
        shared_data_unlock();

        snap.node_id = 2;

        ESP_LOGI(TAG,
                 "TX -> AX:%.2f AY:%.2f | LAT:%.6f LON:%.6f ALT:%.1f",
                 snap.acc_x, snap.acc_y,
                 snap.latitud, snap.longitud, snap.altitud);

        esp_now_send(receiver_mac, (uint8_t *)&snap, sizeof(snap));

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}