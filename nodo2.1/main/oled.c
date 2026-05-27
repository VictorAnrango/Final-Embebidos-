#include "oled.h"
#include "shared_data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <stdio.h>

#define TAG "OLED"

static ssd1306_handle_t oled_handle;

void oled_init(ssd1306_handle_t handle)
{
    oled_handle = handle;
}

void task_oled(void *pvParameters)
{
    char line[32];

    while (1)
    {
        nodo2_data_t snap;

        shared_data_lock();
        snap = shared_data;
        shared_data_unlock();

        ssd1306_clear_screen(oled_handle, false);

        sprintf(line, "NODO 2");
        ssd1306_draw_string(oled_handle, 0,  0,
                            (const uint8_t *)line, 16, true);

        sprintf(line, "AX%.2f AY%.2f", snap.acc_x, snap.acc_y);
        ssd1306_draw_string(oled_handle, 0, 18,
                            (const uint8_t *)line, 12, true);

        sprintf(line, "AZ %.2f", snap.acc_z);
        ssd1306_draw_string(oled_handle, 0, 30,
                            (const uint8_t *)line, 12, true);

        sprintf(line, "LAT %.4f", snap.latitud);
        ssd1306_draw_string(oled_handle, 0, 44,
                            (const uint8_t *)line, 12, true);

        sprintf(line, "ALT %.0fm", snap.altitud);
        ssd1306_draw_string(oled_handle, 0, 56,
                            (const uint8_t *)line, 12, true);

        ssd1306_refresh_gram(oled_handle);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}