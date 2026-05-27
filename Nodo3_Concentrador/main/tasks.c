#include "tasks.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "shared_types.h"
#include "http_client.h"

static const char *TAG = "TASKS";

// ============================================================
// server_task
// Envía los datos al servidor cada 10 s si ya hay datos.
// ============================================================

static void server_task(void *pvParameters)
{
    ESP_LOGI(TAG, "server_task iniciada");

    while (1)
    {
        if (nodo1_recibido || nodo2_recibido)
        {
            http_post_data();
        }
        else
        {
            ESP_LOGD(TAG, "Sin datos aún, esperando...");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ============================================================
// threshold_task
// Consulta los umbrales al servidor cada 10 s para mantenerlos
// actualizados sin necesidad de reiniciar el nodo.
// ============================================================

static void threshold_task(void *pvParameters)
{
    ESP_LOGI(TAG, "threshold_task iniciada");

    while (1)
    {
        http_fetch_thresholds();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ============================================================
// API pública
// ============================================================

void tasks_start(void)
{
    xTaskCreate(server_task,
                "server_task",
                8192,           // stack: 8 KB (JSON + HTTP)
                NULL,
                5,              // prioridad
                NULL);

    xTaskCreate(threshold_task,
                "threshold_task",
                4096,           // stack: 4 KB
                NULL,
                4,
                NULL);

    ESP_LOGI(TAG, "Tareas FreeRTOS lanzadas");
}