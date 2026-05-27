#include "http_client.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "shared_types.h"

// ============================================================
// URLs del servidor
// ============================================================

#define SERVER_URL    "http://192.168.18.88:8080/api/data"
#define THRESHOLD_URL "http://192.168.18.88:8080/api/thresholds"

static const char *TAG = "HTTP";

// ============================================================
// Umbrales en memoria (con mutex)
// ============================================================

static SemaphoreHandle_t thresholds_mutex = NULL;

// Valores por defecto — deben coincidir con DEFAULT_THRESHOLDS en alerts.py
static int   cached_bpm_min  =  40;
static int   cached_bpm_max  = 120;
static float cached_spo2_min =  90.0f;
static float cached_mov_max  =   2.5f;

// Buffer para respuesta HTTP del GET de umbrales
static char thresholds_buf[512];
static int  thresholds_buf_len = 0;

// ============================================================
// Inicialización interna (lazy, llamada la primera vez)
// ============================================================

static void ensure_mutex(void)
{
    if (thresholds_mutex == NULL)
    {
        thresholds_mutex = xSemaphoreCreateMutex();
    }
}

// ============================================================
// HTTP GET umbrales — event handler
// ============================================================

static esp_err_t threshold_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0)
    {
        int space = (int)sizeof(thresholds_buf) - thresholds_buf_len - 1;
        if (space > 0)
        {
            int copy = evt->data_len < space ? evt->data_len : space;
            memcpy(thresholds_buf + thresholds_buf_len, evt->data, copy);
            thresholds_buf_len += copy;
            thresholds_buf[thresholds_buf_len] = '\0';
        }
    }
    return ESP_OK;
}

// ============================================================
// API pública
// ============================================================

void http_fetch_thresholds(void)
{
    ensure_mutex();

    thresholds_buf_len = 0;
    memset(thresholds_buf, 0, sizeof(thresholds_buf));

    esp_http_client_config_t cfg = {
        .url           = THRESHOLD_URL,
        .method        = HTTP_METHOD_GET,
        .timeout_ms    = 5000,
        .event_handler = threshold_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK && thresholds_buf_len > 0)
    {
        cJSON *root = cJSON_Parse(thresholds_buf);
        if (root)
        {
            if (xSemaphoreTake(thresholds_mutex, pdMS_TO_TICKS(200)) == pdTRUE)
            {
                cJSON *item;

                // --- CORREGIDO: leer los campos que realmente envía el servidor ---
                item = cJSON_GetObjectItem(root, "bpm_min");
                if (item) cached_bpm_min = item->valueint;

                item = cJSON_GetObjectItem(root, "bpm_max");
                if (item) cached_bpm_max = item->valueint;

                item = cJSON_GetObjectItem(root, "spo2_min");
                if (item) cached_spo2_min = (float)item->valuedouble;

                item = cJSON_GetObjectItem(root, "movimiento_max");
                if (item) cached_mov_max = (float)item->valuedouble;

                xSemaphoreGive(thresholds_mutex);
            }

            ESP_LOGI(TAG, "Umbrales: bpm=[%d-%d] spo2_min=%.1f mov_max=%.2f",
                     cached_bpm_min, cached_bpm_max, cached_spo2_min, cached_mov_max);

            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGW(TAG, "Error parseando JSON de umbrales");
        }
    }
    else
    {
        ESP_LOGW(TAG, "GET umbrales falló: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void http_get_thresholds_values(int   *bpm_min,
                                int   *bpm_max,
                                float *spo2_min,
                                float *mov_max)
{
    ensure_mutex();

    if (xSemaphoreTake(thresholds_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        *bpm_min  = cached_bpm_min;
        *bpm_max  = cached_bpm_max;
        *spo2_min = cached_spo2_min;
        *mov_max  = cached_mov_max;
        xSemaphoreGive(thresholds_mutex);
    }
    else
    {
        // Fallback a valores por defecto
        *bpm_min  =  40;
        *bpm_max  = 120;
        *spo2_min =  90.0f;
        *mov_max  =   2.5f;
    }
}

void http_post_data(void)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "n1_acc_x",     datos_nodo1.acc_x);
    cJSON_AddNumberToObject(root, "n1_acc_y",     datos_nodo1.acc_y);
    cJSON_AddNumberToObject(root, "n1_acc_z",     datos_nodo1.acc_z);
    cJSON_AddNumberToObject(root, "n1_gyro_x",    datos_nodo1.gyro_x);
    cJSON_AddNumberToObject(root, "n1_gyro_y",    datos_nodo1.gyro_y);
    cJSON_AddNumberToObject(root, "n1_gyro_z",    datos_nodo1.gyro_z);
    cJSON_AddNumberToObject(root, "n1_spo2",      datos_nodo1.spo2);
    cJSON_AddNumberToObject(root, "n1_bpm",       datos_nodo1.bpm);
    cJSON_AddNumberToObject(root, "n1_bpm_valid", datos_nodo1.bpm_valid);

    cJSON_AddNumberToObject(root, "n2_acc_x",     datos_nodo2.acc_x);
    cJSON_AddNumberToObject(root, "n2_acc_y",     datos_nodo2.acc_y);
    cJSON_AddNumberToObject(root, "n2_acc_z",     datos_nodo2.acc_z);
    cJSON_AddNumberToObject(root, "n2_gyro_x",    datos_nodo2.gyro_x);
    cJSON_AddNumberToObject(root, "n2_gyro_y",    datos_nodo2.gyro_y);
    cJSON_AddNumberToObject(root, "n2_gyro_z",    datos_nodo2.gyro_z);
    cJSON_AddNumberToObject(root, "n2_latitud",   datos_nodo2.latitud);
    cJSON_AddNumberToObject(root, "n2_longitud",  datos_nodo2.longitud);
    cJSON_AddNumberToObject(root, "n2_altitud",   datos_nodo2.altitud);
    cJSON_AddNumberToObject(root, "n2_velocidad", datos_nodo2.velocidad);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_http_client_config_t cfg = {
        .url        = SERVER_URL,
        .method     = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "POST OK status=%d",
                 esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "POST falló: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json);
}