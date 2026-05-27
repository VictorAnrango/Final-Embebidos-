#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "qmi8658.h"
#include "pulse_sensor.h"
#include "display.h"
#include "touch.h"
#include "espnow_send.h"

static const char *TAG = "MAIN";

/* ── Estado global de sensores ───────────────────────────────────── */
static qmi8658_data_t  g_imu   = {0};
static pulse_data_t    g_pulse = {0};

/* ── Máquina de estados de la UI ─────────────────────────────────── */
typedef enum {
    UI_DASHBOARD = 0,
    UI_NODO1_APP
} ui_state_t;

static ui_state_t      g_ui_state = UI_DASHBOARD;
static display_page_t  g_page     = PAGE_IMU;   /* página horizontal activa */
static display_sub_t   g_sub      = SUB_TOP;    /* sección vertical activa  */

/* ── Mutex ───────────────────────────────────────────────────────── */
static SemaphoreHandle_t g_mutex     = NULL;
static SemaphoreHandle_t g_lcd_mutex = NULL;

/* ── Buffer ESP-NOW ──────────────────────────────────────────────── */
static qmi8658_data_t g_espnow_imu   = {0};
static pulse_data_t   g_espnow_pulse = {0};
static bool           g_espnow_data_ready = false;

/* ═══════════════════════════════════════════════════════════════════
 *  TAREA: muestreo continuo del sensor de pulso
 * ═══════════════════════════════════════════════════════════════════*/
static void task_pulse_sampling(void *arg) {
    for (;;) {
        pulse_sensor_sample();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  UTILIDAD: toque dentro de un círculo
 * ═══════════════════════════════════════════════════════════════════*/
static bool touch_inside_circle(uint16_t x, uint16_t y,
                                uint16_t cx, uint16_t cy, uint16_t r) {
    int dx = (int)x - (int)cx;
    int dy = (int)y - (int)cy;
    return (dx*dx + dy*dy) <= (int)(r*r);
}

/* ═══════════════════════════════════════════════════════════════════
 *  TAREA: lectura del panel táctil y cambio de estado
 * ═══════════════════════════════════════════════════════════════════*/
static void task_touch(void *arg) {
    touch_gesture_t last_gesture  = GESTURE_NONE;
    TickType_t      last_change   = 0;
    const TickType_t COOLDOWN     = pdMS_TO_TICKS(500);

    /* Coordenadas del botón central del dashboard */
    const uint16_t DASH_CX = 120, DASH_CY = 120, DASH_R = 45;

    for (;;) {
        touch_data_t td  = touch_read();
        TickType_t   now = xTaskGetTickCount();

        if ((now - last_change) > COOLDOWN) {

            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100))) {

                if (g_ui_state == UI_DASHBOARD) {
                    /* Solo entra a la app si toca el botón central */
                    if (touch_inside_circle(td.x, td.y, DASH_CX, DASH_CY, DASH_R)) {
                        g_ui_state = UI_NODO1_APP;
                        g_page     = PAGE_IMU;
                        g_sub      = SUB_TOP;
                        last_change = now;
                    }

                } else {  /* UI_NODO1_APP */

                    /* Botón de regreso (esquina superior izquierda) */
                    if (touch_inside_circle(td.x, td.y, 28, 28, 25)) {
                        g_ui_state = UI_DASHBOARD;
                        last_change = now;

                    } else if (td.gesture != GESTURE_NONE &&
                               td.gesture != last_gesture) {

                        /* Navegación HORIZONTAL entre páginas */
                        if (td.gesture == GESTURE_SWIPE_LEFT) {
                            g_page = PAGE_PULSE;
                            g_sub  = SUB_TOP;   /* resetea sección al cambiar página */
                            last_change = now;

                        } else if (td.gesture == GESTURE_SWIPE_RIGHT) {
                            g_page = PAGE_IMU;
                            g_sub  = SUB_TOP;
                            last_change = now;

                        /* Navegación VERTICAL dentro de la página (scroll) */
                        } else if (td.gesture == GESTURE_SWIPE_UP) {
                            if (g_sub == SUB_TOP) {
                                g_sub = SUB_BOT;
                                last_change = now;
                            }

                        } else if (td.gesture == GESTURE_SWIPE_DOWN) {
                            if (g_sub == SUB_BOT) {
                                g_sub = SUB_TOP;
                                last_change = now;
                            }
                        }
                    }
                }

                xSemaphoreGive(g_mutex);
            }
        }

        last_gesture = (td.gesture == GESTURE_NONE) ? GESTURE_NONE : td.gesture;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  TAREA: refresco de pantalla cuando cambia el estado de la UI
 *  (página, sub-sección o estado dashboard/app)
 * ═══════════════════════════════════════════════════════════════════*/
static void task_display_refresh(void *arg) {
    display_page_t  last_page     = (display_page_t)-1;
    ui_state_t      last_ui_state = (ui_state_t)-1;
    display_sub_t   last_sub      = (display_sub_t)-1;

    for (;;) {
        display_page_t  cur_page;
        ui_state_t      cur_ui;
        display_sub_t   cur_sub;
        qmi8658_data_t  local_imu;
        pulse_data_t    local_pulse;

        /* Lee estado con mutex */
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100))) {
            cur_page    = g_page;
            cur_ui      = g_ui_state;
            cur_sub     = g_sub;
            local_imu   = g_imu;
            local_pulse = g_pulse;
            xSemaphoreGive(g_mutex);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ¿Cambió algo? */
        bool ui_changed   = (cur_ui  != last_ui_state);
        bool page_changed = (cur_page != last_page);
        bool sub_changed  = (cur_sub  != last_sub);

        if (ui_changed || page_changed || sub_changed) {

            if (xSemaphoreTake(g_lcd_mutex, pdMS_TO_TICKS(500))) {

                if (cur_ui == UI_DASHBOARD) {
                    /* Volvió al dashboard: dibuja completo */
                    display_show_dashboard();

                } else if (ui_changed || page_changed) {
                    /* Cambió de dashboard→app o de página horizontal:
                     * dibuja la nueva página sin animación de scroll */
                    display_show_page(&local_imu, &local_pulse,
                                      cur_page, cur_sub);

                } else if (sub_changed) {
                    /* Solo cambió la sub-sección vertical:
                     * anima el scroll antes de dibujar el destino */
                    display_scroll_to_sub(&local_imu, &local_pulse,
                                          cur_page, cur_sub);
                }

                xSemaphoreGive(g_lcd_mutex);
            }

            last_ui_state = cur_ui;
            last_page     = cur_page;
            last_sub      = cur_sub;
        }

        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  TAREA: ciclo principal de sensores + refresco parcial de datos
 * ═══════════════════════════════════════════════════════════════════*/
static void task_main_cycle(void *arg) {
    for (;;) {
        qmi8658_data_t local_imu;
        pulse_data_t   local_pulse;

        /* Lee sensores */
        if (qmi8658_read(&local_imu) != ESP_OK)
            ESP_LOGW(TAG, "Error QMI8658");
        local_pulse = pulse_sensor_get();

        /* Guarda en globals */
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100))) {
            g_imu   = local_imu;
            g_pulse = local_pulse;
            xSemaphoreGive(g_mutex);
        }

        ESP_LOGI("SENSORES",
            "ACC[X=%+.2f Y=%+.2f Z=%+.2f] | GYR[X=%+.1f Y=%+.1f Z=%+.1f]"
            " | BPM=%d | SpO2=%d%% | Pulso=%s",
            local_imu.acc_x, local_imu.acc_y, local_imu.acc_z,
            local_imu.gyro_x, local_imu.gyro_y, local_imu.gyro_z,
            local_pulse.bpm, local_pulse.spo2,
            local_pulse.valid ? "OK" : "SIN SENAL");

        /* Lee estado actual de la UI */
        display_page_t cur_page;
        ui_state_t     cur_ui;
        display_sub_t  cur_sub;

        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100))) {
            cur_page = g_page;
            cur_ui   = g_ui_state;
            cur_sub  = g_sub;
            xSemaphoreGive(g_mutex);
        } else {
            cur_page = PAGE_IMU;
            cur_ui   = UI_DASHBOARD;
            cur_sub  = SUB_TOP;
        }

        /* Refresco parcial de zonas dinámicas (solo cuando está en app) */
        if (cur_ui == UI_NODO1_APP) {
            /* Alimenta el buffer circular de la gráfica BPM */
            if (local_pulse.valid && local_pulse.bpm > 0)
                display_push_bpm_sample(local_pulse.bpm);

            if (xSemaphoreTake(g_lcd_mutex, pdMS_TO_TICKS(500))) {
                display_update_data(&local_imu, &local_pulse,
                                    cur_page, cur_sub);
                xSemaphoreGive(g_lcd_mutex);
            }
        }

        /* Prepara datos para ESP-NOW */
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100))) {
            g_espnow_imu       = local_imu;
            g_espnow_pulse     = local_pulse;
            g_espnow_data_ready = true;
            xSemaphoreGive(g_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  TAREA: envío ESP-NOW
 * ═══════════════════════════════════════════════════════════════════*/
static void task_espnow_send(void *arg) {
    for (;;) {
        qmi8658_data_t local_imu;
        pulse_data_t   local_pulse;
        bool enviar = false;

        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100))) {
            if (g_espnow_data_ready) {
                local_imu   = g_espnow_imu;
                local_pulse = g_espnow_pulse;
                g_espnow_data_ready = false;
                enviar = true;
            }
            xSemaphoreGive(g_mutex);
        }

        if (enviar) {
            esp_err_t ret = espnow_send_data(&local_imu, &local_pulse);
            if (ret != ESP_OK)
                ESP_LOGW("ESPNOW", "Fallo ESP-NOW ignorado");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  PUNTO DE ENTRADA
 * ═══════════════════════════════════════════════════════════════════*/
void app_main(void) {
    ESP_LOGI(TAG, "=== NODO 1 INICIANDO ===");

    g_mutex     = xSemaphoreCreateMutex();
    configASSERT(g_mutex != NULL);
    g_lcd_mutex = xSemaphoreCreateMutex();
    configASSERT(g_lcd_mutex != NULL);

    ESP_ERROR_CHECK(qmi8658_init());
    ESP_ERROR_CHECK(pulse_sensor_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(touch_init());
    ESP_ERROR_CHECK(espnow_init());

    ESP_LOGI(TAG, "Perifericos OK");

    xTaskCreatePinnedToCore(task_pulse_sampling,  "pulse",    2048, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(task_touch,           "touch",    3072, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(task_display_refresh, "disp_ref", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_main_cycle,      "main",     4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(task_espnow_send,     "espnow_tx",4096, NULL, 2, NULL, 0);

    ESP_LOGI(TAG, "Sistema corriendo");
}