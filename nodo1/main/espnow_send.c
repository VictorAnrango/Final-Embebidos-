#include "espnow_send.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG  = "ESPNOW";
static uint8_t nodo3_mac[] = NODO3_MAC;

static void on_data_sent(const wifi_tx_info_t *tx_info,
                         esp_now_send_status_t status)
{
    ESP_LOGI(TAG,
             "Entrega ESP-NOW -> %s | MAC destino: %02X:%02X:%02X:%02X:%02X:%02X",
             status == ESP_NOW_SEND_SUCCESS ? "CONECTADO / OK" : "NO CONECTADO / FALLO",
             nodo3_mac[0],
             nodo3_mac[1],
             nodo3_mac[2],
             nodo3_mac[3],
             nodo3_mac[4],
             nodo3_mac[5]);
}

uint8_t obtener_canal_wifi(const char *ssid_objetivo)
{
    uint16_t ap_count = 0;
    wifi_ap_record_t ap_info[20];

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    ESP_LOGI(TAG, "Buscando canal WiFi para ESP-NOW...");

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "Error escaneando WiFi: %s",
                 esp_err_to_name(ret));
        return 1;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

    if (ap_count > 20)
    {
        ap_count = 20;
    }

    ESP_ERROR_CHECK(
        esp_wifi_scan_get_ap_records(&ap_count, ap_info)
    );

    for (int i = 0; i < ap_count; i++)
    {
       //ESP_LOGI(TAG,
         //        "SSID: %s Canal: %d",
           //      ap_info[i].ssid,
             //    ap_info[i].primary);

        if (strcmp((char *)ap_info[i].ssid, ssid_objetivo) == 0)
        {
            ESP_LOGI(TAG,
            "Canal ESP-NOW encontrado: %d | Red base: %s",
            ap_info[i].primary,
            ssid_objetivo);
        }
    }

    ESP_LOGW(TAG,
             "No se encontro SSID %s. Usando canal 1",
             ssid_objetivo);

    return 1;
}

esp_err_t espnow_init(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    ret = esp_event_loop_create_default();

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return ret;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ret = esp_wifi_init(&cfg);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    uint8_t canal = obtener_canal_wifi(WIFI_SSID_OBJETIVO);

    ESP_ERROR_CHECK(
        esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE)
    );

    ESP_LOGI(TAG,
         "ESP-NOW configurado en canal: %d",
         canal);

    ret = esp_now_init();

    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_ERROR_CHECK(
        esp_now_register_send_cb(on_data_sent)
    );

    esp_now_peer_info_t peer = {0};

    memcpy(peer.peer_addr,
           nodo3_mac,
           6);

    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    ret = esp_now_add_peer(&peer);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_LOGI(TAG,
         "Conexion ESP-NOW lista | MAC Nodo 3: %02X:%02X:%02X:%02X:%02X:%02X",
         nodo3_mac[0],
         nodo3_mac[1],
         nodo3_mac[2],
         nodo3_mac[3],
         nodo3_mac[4],
         nodo3_mac[5]);

    return ESP_OK;
}

esp_err_t espnow_send_data(const qmi8658_data_t *imu,
                           const pulse_data_t *pulse)
{
    nodo1_packet_t pkt = {
        .node_id = 1,

        .acc_x = imu->acc_x,
        .acc_y = imu->acc_y,
        .acc_z = imu->acc_z,

        .gyro_x = imu->gyro_x,
        .gyro_y = imu->gyro_y,
        .gyro_z = imu->gyro_z,

        .spo2 = (float)pulse->spo2,

        .bpm = pulse->bpm,
        .bpm_valid = pulse->valid,
    };

    esp_err_t ret = esp_now_send(
        nodo3_mac,
        (uint8_t *)&pkt,
        sizeof(pkt)
    );

    if (ret != ESP_OK)
{
    ESP_LOGE(TAG,
             "Envio ESP-NOW no iniciado | Error: %s | MAC destino: %02X:%02X:%02X:%02X:%02X:%02X",
             esp_err_to_name(ret),
             nodo3_mac[0],
             nodo3_mac[1],
             nodo3_mac[2],
             nodo3_mac[3],
             nodo3_mac[4],
             nodo3_mac[5]);
}

    return ret;
}