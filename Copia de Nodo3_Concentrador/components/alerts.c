#include "alerts.h"

#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "shared_types.h"
#include "leds.h"
#include "oled.h"
#include "http_client.h"   // para leer umbrales actuales

static const char *TAG = "ALERTS";

// ============================================================
// Tiempos y conteos (en milisegundos)
// ============================================================

#define ALERTA_PULSO_TIEMPO_MS    60000   // 1 minuto
#define ALERTA_SPO2_TIEMPO_MS    120000   // 2 minutos
#define ALERTA_MOV_TIEMPO_MS      30000   // 30 segundos

#define ALERTA_PULSO_CLEAR_COUNT      3
#define ALERTA_SPO2_CLEAR_COUNT       5
#define ALERTA_MOV_CLEAR_COUNT        2

// ============================================================
// Estado interno
// ============================================================

typedef struct {
    // Alertas activas
    bool    alerta_pulso_activa;
    bool    alerta_spo2_activa;
    bool    alerta_mov_activa;

    // Inicio de la racha problemática (-1 = sin racha en curso)
    int64_t pulso_inicio_ms;
    int64_t spo2_inicio_ms;
    int64_t mov_inicio_ms;

    // Contador de datos buenos consecutivos para desactivar
    int     pulso_clear_count;
    int     spo2_clear_count;
    int     mov_clear_count;

    // Últimos valores para mostrar en OLED
    int     ultimo_bpm;
    float   ultima_spo2;
} alert_state_t;

static alert_state_t state;
static SemaphoreHandle_t alert_mutex = NULL;

// ============================================================
// Helpers
// ============================================================

static float magnitud(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

// ============================================================
// API pública
// ============================================================

void alerts_init(void)
{
    alert_mutex = xSemaphoreCreateMutex();

    state = (alert_state_t){
        .alerta_pulso_activa = false,
        .alerta_spo2_activa  = false,
        .alerta_mov_activa   = false,
        .pulso_inicio_ms     = -1,
        .spo2_inicio_ms      = -1,
        .mov_inicio_ms       = -1,
        .pulso_clear_count   = 0,
        .spo2_clear_count    = 0,
        .mov_clear_count     = 0,
        .ultimo_bpm          = 0,
        .ultima_spo2         = 0.0f,
    };

    ESP_LOGI(TAG, "Módulo de alertas inicializado");
}

void alerts_evaluate(void)
{
    int64_t ahora_ms = esp_timer_get_time() / 1000LL;

    // --- Leer umbrales actuales (thread-safe desde http_client) ---
    int   bpm_max;
    float spo2_max;
    float mov_max;
    http_get_thresholds_values(&bpm_max, &spo2_max, &mov_max);

    // --- Leer datos de sensores ---
    // (datos_nodo1 y datos_nodo2 son extern de main.c, protegidos por
    //  el mutex que gestiona wifi_espnow antes de llamar a esta función)
    int   bpm       = datos_nodo1.bpm;
    int   bpm_valid = datos_nodo1.bpm_valid;
    float spo2      = datos_nodo1.spo2;
    float mag_n1    = magnitud(datos_nodo1.acc_x,
                               datos_nodo1.acc_y,
                               datos_nodo1.acc_z);
    float mag_n2    = magnitud(datos_nodo2.acc_x,
                               datos_nodo2.acc_y,
                               datos_nodo2.acc_z);

    if (xSemaphoreTake(alert_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    state.ultimo_bpm  = bpm;
    state.ultima_spo2 = spo2;

    // ------------------------------------------------------------------
    // ALERTA PULSO
    // Activa si BPM > bpm_max durante >= ALERTA_PULSO_TIEMPO_MS.
    // Un dato <= bpm_max reinicia el contador de tiempo.
    // Desactiva con ALERTA_PULSO_CLEAR_COUNT datos buenos consecutivos.
    // ------------------------------------------------------------------
    if (nodo1_recibido && bpm_valid)
    {
        if (bpm > bpm_max)
        {
            state.pulso_clear_count = 0;

            if (!state.alerta_pulso_activa)
            {
                if (state.pulso_inicio_ms < 0)
                {
                    state.pulso_inicio_ms = ahora_ms;
                }
                else if ((ahora_ms - state.pulso_inicio_ms) >= ALERTA_PULSO_TIEMPO_MS)
                {
                    state.alerta_pulso_activa = true;
                    ESP_LOGW(TAG, "ALERTA PULSO activada BPM=%d", bpm);
                }
            }
        }
        else
        {
            state.pulso_inicio_ms = -1;   // reinicia conteo de tiempo

            if (state.alerta_pulso_activa)
            {
                state.pulso_clear_count++;
                if (state.pulso_clear_count >= ALERTA_PULSO_CLEAR_COUNT)
                {
                    state.alerta_pulso_activa = false;
                    state.pulso_clear_count   = 0;
                    ESP_LOGI(TAG, "ALERTA PULSO desactivada");
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // ALERTA SPO2
    // Activa si SpO2 > spo2_max durante >= ALERTA_SPO2_TIEMPO_MS.
    // Un dato <= spo2_max reinicia el contador de tiempo.
    // Desactiva con ALERTA_SPO2_CLEAR_COUNT datos buenos consecutivos.
    // ------------------------------------------------------------------
    if (nodo1_recibido)
    {
        if (spo2 > spo2_max)
        {
            state.spo2_clear_count = 0;

            if (!state.alerta_spo2_activa)
            {
                if (state.spo2_inicio_ms < 0)
                {
                    state.spo2_inicio_ms = ahora_ms;
                }
                else if ((ahora_ms - state.spo2_inicio_ms) >= ALERTA_SPO2_TIEMPO_MS)
                {
                    state.alerta_spo2_activa = true;
                    ESP_LOGW(TAG, "ALERTA OXIGENACION activada SpO2=%.1f", spo2);
                }
            }
        }
        else
        {
            state.spo2_inicio_ms = -1;

            if (state.alerta_spo2_activa)
            {
                state.spo2_clear_count++;
                if (state.spo2_clear_count >= ALERTA_SPO2_CLEAR_COUNT)
                {
                    state.alerta_spo2_activa = false;
                    state.spo2_clear_count   = 0;
                    ESP_LOGI(TAG, "ALERTA OXIGENACION desactivada");
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // ALERTA MOVIMIENTO
    // Activa si mag(acc_n1) > mov_max O mag(acc_n2) > mov_max
    // durante >= ALERTA_MOV_TIEMPO_MS.
    // Un dato de bajo movimiento reinicia el contador de tiempo.
    // Desactiva con ALERTA_MOV_CLEAR_COUNT datos bajos consecutivos.
    // ------------------------------------------------------------------
    bool alto_mov = (nodo1_recibido && mag_n1 > mov_max) ||
                    (nodo2_recibido && mag_n2 > mov_max);

    if (alto_mov)
    {
        state.mov_clear_count = 0;

        if (!state.alerta_mov_activa)
        {
            if (state.mov_inicio_ms < 0)
            {
                state.mov_inicio_ms = ahora_ms;
            }
            else if ((ahora_ms - state.mov_inicio_ms) >= ALERTA_MOV_TIEMPO_MS)
            {
                state.alerta_mov_activa = true;
                ESP_LOGW(TAG, "ALERTA MOVIMIENTO activada");
            }
        }
    }
    else
    {
        state.mov_inicio_ms = -1;

        if (state.alerta_mov_activa)
        {
            state.mov_clear_count++;
            if (state.mov_clear_count >= ALERTA_MOV_CLEAR_COUNT)
            {
                state.alerta_mov_activa = false;
                state.mov_clear_count   = 0;
                ESP_LOGI(TAG, "ALERTA MOVIMIENTO desactivada");
            }
        }
    }

    // Snapshot para actualizar OLED y LEDs fuera del mutex
    bool ap  = state.alerta_pulso_activa;
    bool as2 = state.alerta_spo2_activa;
    bool am  = state.alerta_mov_activa;
    int  b   = state.ultimo_bpm;
    float s  = state.ultima_spo2;

    xSemaphoreGive(alert_mutex);

    // --- Actualizar periféricos ---
    oled_refresh(ap, as2, am, b, s);
    leds_set_alerta(ap || as2 || am);
}

void alerts_get_state(bool *pulso, bool *spo2, bool *movimiento)
{
    if (xSemaphoreTake(alert_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        *pulso      = state.alerta_pulso_activa;
        *spo2       = state.alerta_spo2_activa;
        *movimiento = state.alerta_mov_activa;
        xSemaphoreGive(alert_mutex);
    }
    else
    {
        *pulso = *spo2 = *movimiento = false;
    }
}