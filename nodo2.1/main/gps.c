#include "gps.h"
#include "shared_data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>

#define TAG "GPS"

#define GPS_UART_NUM  UART_NUM_2
#define GPS_RX_PIN    17
#define GPS_TX_PIN    16
#define GPS_BAUD_RATE 9600
#define GPS_BUF_SIZE  512

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

void gps_init(void)
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

void task_gps(void *pvParameters)
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
                    shared_data_lock();
                    shared_data.latitud  = lat;
                    shared_data.longitud = lon;
                    shared_data.altitud  = alt;
                    shared_data_unlock();
                    ESP_LOGI(TAG, "Fix -> LAT:%.6f LON:%.6f ALT:%.1fm",
                             lat, lon, alt);
                }
                i = 0;
            } else if (c != '\r') {
                if (i < (int)sizeof(buffer) - 1)
                    buffer[i++] = (char)c;
            }
        }
    }
}