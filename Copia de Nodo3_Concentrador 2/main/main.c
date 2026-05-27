#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/i2c.h"
#include "driver/gpio.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_client.h"

#include "ssd1306.h"
#include "cJSON.h"

static const char *TAG = "NODO3";
static char thresholds_response[512];
static int thresholds_response_len = 0;

// ================= WIFI / SERVER =================

#define WIFI_SSID "VILLA_MARIA"
#define WIFI_PASS "12345678"

#define SERVER_URL    "http://192.168.18.88:8080/api/data"
#define THRESHOLD_URL "http://192.168.18.88:8080/api/thresholds"

// ================= LEDS =================

#define LED_BLANCO_GPIO 25
#define LED_ROJO_GPIO   26

// ================= WIFI EVENT =================

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// ================= UMBRALES =================

int bpm_min = 60;
int bpm_max = 120;

float spo2_min = 90.0;
float spo2_max = 100.0;
float movimiento_max = 2.5;

// ================= DATOS NODO 1 =================

typedef struct __attribute__((packed)) {
    int node_id;

    float acc_x;
    float acc_y;
    float acc_z;

    float gyro_x;
    float gyro_y;
    float gyro_z;

    float spo2;

    int bpm;
    int bpm_valid;
} nodo1_data_t;

// ================= DATOS NODO 2 =================

typedef struct __attribute__((packed)) {
    int node_id;

    float acc_x;
    float acc_y;
    float acc_z;

    float gyro_x;
    float gyro_y;
    float gyro_z;

    float latitud;
    float longitud;
    float altitud;
    float velocidad;
} nodo2_data_t;

// ================= VARIABLES GLOBALES =================

nodo1_data_t datos_nodo1 = {0};
nodo2_data_t datos_nodo2 = {0};

bool nodo1_recibido = false;
bool nodo2_recibido = false;

// ================= LEDS =================

void leds_init(void)
{
    gpio_reset_pin(LED_BLANCO_GPIO);
    gpio_reset_pin(LED_ROJO_GPIO);

    gpio_set_direction(LED_BLANCO_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ROJO_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_level(LED_BLANCO_GPIO, 1);
    gpio_set_level(LED_ROJO_GPIO, 0);

    ESP_LOGI(TAG, "LEDs iniciados");
}

void actualizar_leds_alerta(void)
{
    bool alerta_pulso = false;
    bool alerta_spo2 = false;

    if (nodo1_recibido)
    {
        if (datos_nodo1.bpm_valid)
        {
            if (datos_nodo1.bpm < bpm_min || datos_nodo1.bpm > bpm_max)
            {
                alerta_pulso = true;
            }
        }

        if (datos_nodo1.spo2 < spo2_min || datos_nodo1.spo2 > spo2_max)
        {
            alerta_spo2 = true;
        }
    }

    if (alerta_pulso || alerta_spo2)
    {
        gpio_set_level(LED_ROJO_GPIO, 1);
        gpio_set_level(LED_BLANCO_GPIO, 0);
    }
    else
    {
        gpio_set_level(LED_ROJO_GPIO, 0);
        gpio_set_level(LED_BLANCO_GPIO, 1);
    }
}

// ================= OLED I2C =================

#define I2C_MASTER_SDA_IO 4
#define I2C_MASTER_SCL_IO 15
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define OLED_ADDR 0x3C

ssd1306_handle_t oled = NULL;

// ================= I2C INIT =================

void i2c_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C param config fallo: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C driver install fallo: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "I2C iniciado");
}

// ================= OLED EVENTOS =================

void oled_show_event(const char *titulo,
                     const char *linea1,
                     const char *linea2)
{
    if (oled == NULL)
    {
        return;
    }

    ssd1306_clear_screen(oled, false);

    ssd1306_draw_string(
        oled,
        0,
        0,
        (const uint8_t *)titulo,
        16,
        true
    );

    ssd1306_draw_string(
        oled,
        0,
        24,
        (const uint8_t *)linea1,
        12,
        true
    );

    ssd1306_draw_string(
        oled,
        0,
        44,
        (const uint8_t *)linea2,
        12,
        true
    );

    ssd1306_refresh_gram(oled);
}

// ================= OLED INIT =================

void oled_init(void)
{
    oled = ssd1306_create(I2C_MASTER_NUM, OLED_ADDR);

    if (oled == NULL) {
        ESP_LOGW(TAG, "OLED no encontrada, continuando sin pantalla");
        return;
    }

    esp_err_t err = ssd1306_init(oled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED init fallo, continuando sin pantalla");
        oled = NULL;
        return;
    }

    ssd1306_clear_screen(oled, false);
    ssd1306_draw_string(oled, 0,  0, (const uint8_t *)"NODO 3",         16, true);
    ssd1306_draw_string(oled, 0, 24, (const uint8_t *)"Sistema iniciado", 12, true);
    ssd1306_draw_string(oled, 0, 44, (const uint8_t *)"Esperando datos",  12, true);
    ssd1306_refresh_gram(oled);

    ESP_LOGI(TAG, "OLED iniciada");
}

// ================= WIFI =================

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *disc =
            (wifi_event_sta_disconnected_t *) event_data;

        ESP_LOGW(TAG,
                 "WiFi desconectado. Reason: %d. Reconectando...",
                 disc->reason);

        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;

        ESP_LOGI(TAG, "IP ESP32: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group,
                            WIFI_CONNECTED_BIT);
    }
}

void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            NULL
        )
    );

    wifi_config_t wifi_config = {0};

    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config)
    );

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Esperando conexion WiFi...");

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    ESP_LOGI(TAG, "WiFi conectado correctamente");

    uint8_t primary;
    wifi_second_chan_t second;

    esp_wifi_get_channel(&primary, &second);

    ESP_LOGI(TAG, "CANAL WIFI: %d", primary);
}

// ================= ESPNOW =================
void on_data_recv(const esp_now_recv_info_t *recv_info,
                  const uint8_t *incomingDataBytes,
                  int len);

void espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());

    ESP_ERROR_CHECK(
        esp_now_register_recv_cb(on_data_recv)
    );

    ESP_LOGI(TAG, "ESPNOW iniciado");
}

// ================= HTTP POST DATA =================

void send_data_to_server(void)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "n1_acc_x", datos_nodo1.acc_x);
    cJSON_AddNumberToObject(root, "n1_acc_y", datos_nodo1.acc_y);
    cJSON_AddNumberToObject(root, "n1_acc_z", datos_nodo1.acc_z);

    cJSON_AddNumberToObject(root, "n1_gyro_x", datos_nodo1.gyro_x);
    cJSON_AddNumberToObject(root, "n1_gyro_y", datos_nodo1.gyro_y);
    cJSON_AddNumberToObject(root, "n1_gyro_z", datos_nodo1.gyro_z);

    cJSON_AddNumberToObject(root, "n1_spo2", datos_nodo1.spo2);
    cJSON_AddNumberToObject(root, "n1_bpm", datos_nodo1.bpm);
    cJSON_AddNumberToObject(root, "n1_bpm_valid", datos_nodo1.bpm_valid);

    cJSON_AddNumberToObject(root, "n2_acc_x", datos_nodo2.acc_x);
    cJSON_AddNumberToObject(root, "n2_acc_y", datos_nodo2.acc_y);
    cJSON_AddNumberToObject(root, "n2_acc_z", datos_nodo2.acc_z);

    cJSON_AddNumberToObject(root, "n2_gyro_x", datos_nodo2.gyro_x);
    cJSON_AddNumberToObject(root, "n2_gyro_y", datos_nodo2.gyro_y);
    cJSON_AddNumberToObject(root, "n2_gyro_z", datos_nodo2.gyro_z);

    cJSON_AddNumberToObject(root, "n2_latitud", datos_nodo2.latitud);
    cJSON_AddNumberToObject(root, "n2_longitud", datos_nodo2.longitud);
    cJSON_AddNumberToObject(root, "n2_altitud", datos_nodo2.altitud);
    cJSON_AddNumberToObject(root, "n2_velocidad", datos_nodo2.velocidad);

    char *json_string = cJSON_PrintUnformatted(root);

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    esp_http_client_set_header(client,
                               "Content-Type",
                               "application/json");

    esp_http_client_set_post_field(client,
                                   json_string,
                                   strlen(json_string));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG,
                 "HTTP POST Status = %d",
                 esp_http_client_get_status_code(client));
                 
        char linea1[32];
        char linea2[32];

        sprintf(linea1, "HTTP %d",
                esp_http_client_get_status_code(client));

        sprintf(linea2, "Datos enviados");

        oled_show_event(
            "SERVIDOR OK",
            linea1,
            linea2
        );
    }
    else
    {
        ESP_LOGE(TAG,
                "HTTP POST failed: %s",
                esp_err_to_name(err));

        oled_show_event(
            "ERROR SERVER",
            "POST fallido",
            "Revisar WiFi/API"
        );
    }
}

// ================= HTTP GET UMBRALES =================
esp_err_t thresholds_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 &&
                thresholds_response_len + evt->data_len < sizeof(thresholds_response))
            {
                memcpy(thresholds_response + thresholds_response_len,
                       evt->data,
                       evt->data_len);

                thresholds_response_len += evt->data_len;
                thresholds_response[thresholds_response_len] = '\0';
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}

void consultar_umbrales(void)
{
    thresholds_response_len = 0;
    memset(thresholds_response, 0, sizeof(thresholds_response));

    esp_http_client_config_t config = {
        .url = THRESHOLD_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .event_handler = thresholds_http_event_handler,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);

        ESP_LOGI(TAG, "GET umbrales status: %d", status);
        ESP_LOGI(TAG, "JSON umbrales: %s", thresholds_response);

        if (thresholds_response_len > 0)
        {
            cJSON *root = cJSON_Parse(thresholds_response);

            if (root)
            {
                cJSON *item;

                item = cJSON_GetObjectItem(root, "bpm_min");
                if (item) bpm_min = item->valueint;

                item = cJSON_GetObjectItem(root, "bpm_max");
                if (item) bpm_max = item->valueint;

                item = cJSON_GetObjectItem(root, "spo2_min");
                if (item) spo2_min = item->valuedouble;

                item = cJSON_GetObjectItem(root, "spo2_max");
                if (item) spo2_max = item->valuedouble;

                item = cJSON_GetObjectItem(root, "movimiento_max");
                if (item) movimiento_max = item->valuedouble;

                ESP_LOGI(TAG,
                         "Umbrales actualizados BPM[%d-%d] SpO2[%.1f-%.1f] MOV[%.2f]",
                         bpm_min,
                         bpm_max,
                         spo2_min,
                         spo2_max,
                         movimiento_max);

                cJSON_Delete(root);
            }
            else
            {
                ESP_LOGW(TAG, "Error parseando JSON umbrales");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Respuesta vacia de umbrales");
        }
    }
    else
    {
        ESP_LOGW(TAG,
                 "Error consultando umbrales: %s",
                 esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// ================= TASKS =================

void server_task(void *pvParameters)
{
    while (1)
    {
        if (nodo1_recibido || nodo2_recibido)
        {
            send_data_to_server();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void threshold_task(void *pvParameters)
{
    while (1)
    {
        consultar_umbrales();
        actualizar_leds_alerta();

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ================= ESPNOW CALLBACK =================

void on_data_recv(const esp_now_recv_info_t *recv_info,
                  const uint8_t *incomingDataBytes,
                  int len)
{
    if (len < sizeof(int))
    {
        ESP_LOGW(TAG, "Paquete demasiado pequeno");
        return;
    }

    int node_id;
    memcpy(&node_id, incomingDataBytes, sizeof(int));

    if (node_id == 1)
    {
        if (len != sizeof(nodo1_data_t))
        {
            ESP_LOGW(TAG,
                     "Nodo 1 tamano incorrecto: %d esperado: %d",
                     len,
                     sizeof(nodo1_data_t));
            return;
        }

        memcpy(&datos_nodo1,
               incomingDataBytes,
               sizeof(nodo1_data_t));

        nodo1_recibido = true;
        char linea1[32];
        char linea2[32];

        sprintf(linea1, "BPM: %d", datos_nodo1.bpm);
        sprintf(linea2, "SpO2: %.1f", datos_nodo1.spo2);

        oled_show_event(
            "RECIBIDO NODO1",
            linea1,
            linea2
        );

        ESP_LOGI(TAG, "===== NODO 1 RECIBIDO =====");

        ESP_LOGI(TAG,
                 "ACC X: %.2f Y: %.2f Z: %.2f",
                 datos_nodo1.acc_x,
                 datos_nodo1.acc_y,
                 datos_nodo1.acc_z);

        ESP_LOGI(TAG,
                 "GYRO X: %.2f Y: %.2f Z: %.2f",
                 datos_nodo1.gyro_x,
                 datos_nodo1.gyro_y,
                 datos_nodo1.gyro_z);

        ESP_LOGI(TAG,
                 "SpO2: %.2f BPM: %d VALID: %d",
                 datos_nodo1.spo2,
                 datos_nodo1.bpm,
                 datos_nodo1.bpm_valid);

        actualizar_leds_alerta();

        return;
    }

    if (node_id == 2)
    {
        if (len != sizeof(nodo2_data_t))
        {
            ESP_LOGW(TAG,
                     "Nodo 2 tamano incorrecto: %d esperado: %d",
                     len,
                     sizeof(nodo2_data_t));
            return;
        }

        memcpy(&datos_nodo2,
               incomingDataBytes,
               sizeof(nodo2_data_t));

        nodo2_recibido = true;
        char linea1[32];
        char linea2[32];

        sprintf(linea1, "LAT: %.4f", datos_nodo2.latitud);
        sprintf(linea2, "VEL: %.1f", datos_nodo2.velocidad);

        oled_show_event(
            "RECIBIDO NODO2",
            linea1,
            linea2
        );

        ESP_LOGI(TAG, "===== NODO 2 RECIBIDO =====");

        ESP_LOGI(TAG,
                 "ACC X: %.2f Y: %.2f Z: %.2f",
                 datos_nodo2.acc_x,
                 datos_nodo2.acc_y,
                 datos_nodo2.acc_z);

        ESP_LOGI(TAG,
                 "LAT: %.6f LON: %.6f VEL: %.2f",
                 datos_nodo2.latitud,
                 datos_nodo2.longitud,
                 datos_nodo2.velocidad);

        return;
    }

    ESP_LOGW(TAG, "Nodo desconocido: %d", node_id);
}

// ================= MAIN =================

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    datos_nodo1.node_id = 1;
    datos_nodo2.node_id = 2;

    ESP_LOGI(TAG, "Nodo 3 iniciando...");

    leds_init();

    i2c_init();
    oled_init();

    wifi_init();

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    ESP_LOGI(TAG,
             "MAC Nodo3: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);

    espnow_init();

    xTaskCreate(server_task,
                "server_task",
                8192,
                NULL,
                5,
                NULL);

    xTaskCreate(threshold_task,
                "threshold_task",
                8192,
                NULL,
                5,
                NULL);

    ESP_LOGI(TAG, "Nodo 3 listo para recibir ESPNOW");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}