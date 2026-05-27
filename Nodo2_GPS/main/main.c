#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/i2c.h"
#include "driver/uart.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "ssd1306.h"

// ================= CONFIG =================

#define WIFI_SSID "RataPunkCM"
#define WIFI_PASS "041024cm"

static const char *TAG = "NODO2";

// I2C
#define I2C_MASTER_SCL_IO    22
#define I2C_MASTER_SDA_IO    21
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_MASTER_FREQ_HZ   400000

#define OLED_ADDR            0x3C
#define MPU6050_ADDR         0x68
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B

// GPS
#define GPS_UART_NUM  UART_NUM_2
#define GPS_RX_PIN    17
#define GPS_TX_PIN    16
#define GPS_BAUD_RATE 9600
#define GPS_BUF_SIZE  512

// Tamaños de stack para cada tarea
#define STACK_MPU   2048
#define STACK_GPS   3072
#define STACK_OLED  2048
#define STACK_SEND  3072

// ================= DATA STRUCT =================

typedef struct {
    int   node_id;
    float acc_x, acc_y, acc_z;
    float gyro_x, gyro_y, gyro_z;
    float latitud;
    float longitud;
    float altitud;
    float velocidad;
} nodo2_data_t;

// Datos compartidos entre tareas
static nodo2_data_t shared_data;
static SemaphoreHandle_t data_mutex;

// Handle OLED compartido
static ssd1306_handle_t oled;

// ================= GPS PARSER =================

static float nmea_to_decimal(const char *coord, const char *hemisferio)
{
    if (!coord || strlen(coord) < 4) return 0.0f;
    float valor   = atof(coord);
    int   grados  = (int)(valor / 100);
    float minutos = valor - (grados * 100);
    float decimal = grados + (minutos / 60.0f);
    if (hemisferio[0] == 'S' || hemisferio[0] == 'W')
        decimal = -decimal;
    return decimal;
}

static bool parse_gga(char *linea, float *lat, float *lon, float *alt)
{
    if (strncmp(linea, "$GPGGA", 6) != 0 &&
        strncmp(linea, "$GNGGA", 6) != 0)
        return false;

    char  buf[128];
    strncpy(buf, linea, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *campos[15];
    int   n   = 0;
    char *tok = strtok(buf, ",");
    while (tok != NULL && n < 15) {
        campos[n++] = tok;
        tok = strtok(NULL, ",");
    }

    if (n < 10) return false;
    if (atoi(campos[6]) == 0) return false;

    *lat = nmea_to_decimal(campos[2], campos[3]);
    *lon = nmea_to_decimal(campos[4], campos[5]);
    *alt = atof(campos[9]);
    return true;
}

// ================= INITS =================

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
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
}

static void mpu6050_init(void)
{
    uint8_t data[2] = {MPU6050_PWR_MGMT_1, 0x00};
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_MASTER_NUM,
                    MPU6050_ADDR, data, 2, pdMS_TO_TICKS(1000)));
    ESP_LOGI(TAG, "MPU6050 iniciado");
}

static void gps_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM,
                                 GPS_TX_PIN, GPS_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM,
                                        GPS_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_LOGI(TAG, "GPS UART2 — RX=GPIO%d TX=GPIO%d", GPS_RX_PIN, GPS_TX_PIN);
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
}

// ================= ESPNOW =================

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

static void espnow_init(void)
{
    uint8_t canal = obtener_canal_wifi(WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE));
    ESP_LOGI(TAG, "Canal ESPNOW: %d", canal);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    ESP_LOGI(TAG, "ESPNOW iniciado");
}

// ================= TAREA MPU6050 =================
// Lee acelerómetro y giroscopio cada 100ms

static void task_mpu(void *pvParameters)
{
    uint8_t reg = MPU6050_ACCEL_XOUT_H;
    uint8_t buf[14];

    while (1)
    {
        esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM,
                            MPU6050_ADDR, &reg, 1, buf, 14,
                            pdMS_TO_TICKS(1000));

        if (err == ESP_OK) {
            float ax = (int16_t)((buf[0]  << 8) | buf[1])  / 16384.0f;
            float ay = (int16_t)((buf[2]  << 8) | buf[3])  / 16384.0f;
            float az = (int16_t)((buf[4]  << 8) | buf[5])  / 16384.0f;
            float gx = (int16_t)((buf[8]  << 8) | buf[9])  / 131.0f;
            float gy = (int16_t)((buf[10] << 8) | buf[11]) / 131.0f;
            float gz = (int16_t)((buf[12] << 8) | buf[13]) / 131.0f;

            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                shared_data.acc_x  = ax;
                shared_data.acc_y  = ay;
                shared_data.acc_z  = az;
                shared_data.gyro_x = gx;
                shared_data.gyro_y = gy;
                shared_data.gyro_z = gz;
                xSemaphoreGive(data_mutex);
            }
        } else {
            ESP_LOGE(TAG, "Error leyendo MPU6050");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ================= TAREA GPS =================
// Lee UART continuamente, parsea GGA, actualiza shared_data

static void task_gps(void *pvParameters)
{
    static char buffer[128];
    static int  i = 0;
    uint8_t     c;

    while (1)
    {
        if (uart_read_bytes(GPS_UART_NUM, &c, 1, pdMS_TO_TICKS(100)) == 1)
        {
            if (c == '\n') {
                buffer[i] = '\0';

                float lat, lon, alt;
                if (parse_gga(buffer, &lat, &lon, &alt)) {
                    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        shared_data.latitud  = lat;
                        shared_data.longitud = lon;
                        shared_data.altitud  = alt;
                        xSemaphoreGive(data_mutex);
                    }
                    ESP_LOGI(TAG, "GPS fix -> LAT:%.6f LON:%.6f ALT:%.1fm",
                             lat, lon, alt);
                }
                i = 0;
            } else if (c != '\r') {
                if (i < (int)sizeof(buffer) - 1)
                    buffer[i++] = (char)c;
            }
        }
        // Sin taskDelay aquí: el timeout del uart_read_bytes (100ms) ya cede CPU
    }
}

// ================= TAREA OLED =================
// Refresca pantalla cada 500ms con los últimos datos

static void task_oled(void *pvParameters)
{
    char line[32];

    while (1)
    {
        nodo2_data_t snap;

        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            snap = shared_data;   // copia local para no bloquear el mutex
            xSemaphoreGive(data_mutex);
        }

        ssd1306_clear_screen(oled, false);

        sprintf(line, "NODO 2");
        ssd1306_draw_string(oled, 0,  0, (const uint8_t *)line, 16, true);

        sprintf(line, "AX%.2f AY%.2f", snap.acc_x, snap.acc_y);
        ssd1306_draw_string(oled, 0, 18, (const uint8_t *)line, 12, true);

        sprintf(line, "AZ %.2f", snap.acc_z);
        ssd1306_draw_string(oled, 0, 30, (const uint8_t *)line, 12, true);

        sprintf(line, "LAT %.4f", snap.latitud);
        ssd1306_draw_string(oled, 0, 44, (const uint8_t *)line, 12, true);

        sprintf(line, "ALT %.0fm", snap.altitud);
        ssd1306_draw_string(oled, 0, 56, (const uint8_t *)line, 12, true);

        ssd1306_refresh_gram(oled);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ================= TAREA ESPNOW =================
// Envía paquete cada 10 segundos

static void task_send(void *pvParameters)
{
    while (1)
    {
        nodo2_data_t snap;

        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            snap = shared_data;
            xSemaphoreGive(data_mutex);
        }

        snap.node_id = 2;

        ESP_LOGI(TAG,
                 "Enviando -> AX:%.2f AY:%.2f AZ:%.2f | LAT:%.6f LON:%.6f ALT:%.1f",
                 snap.acc_x, snap.acc_y, snap.acc_z,
                 snap.latitud, snap.longitud, snap.altitud);

        esp_now_send(receiver_mac, (uint8_t *)&snap, sizeof(snap));

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ================= MAIN =================

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

    // Mutex para datos compartidos
    data_mutex = xSemaphoreCreateMutex();

    // Datos iniciales
    shared_data.node_id  = 2;
    shared_data.latitud  = 0.0f;
    shared_data.longitud = 0.0f;
    shared_data.altitud  = 0.0f;
    shared_data.velocidad = 0.0f;

    // Hardware
    i2c_init();
    mpu6050_init();
    gps_init();

    // OLED
    oled = ssd1306_create(I2C_MASTER_NUM, OLED_ADDR);
    if (oled == NULL) {
        ESP_LOGE(TAG, "Error creando OLED");
        return;
    }
    ESP_ERROR_CHECK(ssd1306_init(oled));
    ssd1306_clear_screen(oled, false);
    ssd1306_refresh_gram(oled);

    // WiFi + ESPNOW
    wifi_init();
    espnow_init();

    // Tareas FreeRTOS
    xTaskCreate(task_mpu,  "mpu",  STACK_MPU,  NULL, 3, NULL);
    xTaskCreate(task_gps,  "gps",  STACK_GPS,  NULL, 3, NULL);
    xTaskCreate(task_oled, "oled", STACK_OLED, NULL, 2, NULL);
    xTaskCreate(task_send, "send", STACK_SEND, NULL, 2, NULL);

    ESP_LOGI(TAG, "Todas las tareas iniciadas");
}