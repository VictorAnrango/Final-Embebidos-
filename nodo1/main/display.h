#pragma once
#include "esp_err.h"
#include "qmi8658.h"
#include "pulse_sensor.h"

/* ─── Páginas principales (navegación horizontal / swipe) ───────────── */
typedef enum {
    PAGE_IMU   = 0,   // Página MOVIMIENTO (acelerómetro + giroscopio)
    PAGE_PULSE = 1,   // Página SALUD      (BPM + gráfica + SpO2)
} display_page_t;

/*
 * Sub-sección dentro de una página vertical.
 *   SUB_TOP  → sección visible por defecto (acelerómetro / BPM+gráfica)
 *   SUB_BOT  → sección revelada al deslizar hacia arriba (giroscopio / SpO2)
 */
typedef enum {
    SUB_TOP = 0,
    SUB_BOT = 1,
} display_sub_t;

/* ─── API pública ──────────────────────────────────────────────────────
 *
 * display_init()
 *   Inicializa bus SPI, GPIO y secuencia GC9A01. Igual que antes.
 *
 * display_show_dashboard()
 *   Renderiza el menú principal premium (launcher).
 *
 * display_show_page(imu, pulse, page, sub)
 *   Dibuja la página completa indicada. Llama cuando cambia page O sub.
 *   'sub' controla qué sección vertical se muestra.
 *
 * display_scroll_to_sub(imu, pulse, page, target_sub)
 *   Anima el deslizamiento vertical entre SUB_TOP y SUB_BOT.
 *   Bloquea hasta completar la animación (~300 ms).
 *
 * display_update_data(imu, pulse, page, sub)
 *   Refresco parcial de solo las zonas numéricas/gráfica. Sin redibujar
 *   elementos estáticos. Llama periódicamente desde task_main_cycle.
 *
 * display_draw_page_indicator(page, total)
 *   Dibuja los puntos indicadores de página en la parte inferior.
 * ─────────────────────────────────────────────────────────────────────*/

esp_err_t display_init(void);

void display_show_dashboard(void);

void display_show_page(const qmi8658_data_t *imu,
                       const pulse_data_t   *pulse,
                       display_page_t        page,
                       display_sub_t         sub);

void display_scroll_to_sub(const qmi8658_data_t *imu,
                           const pulse_data_t   *pulse,
                           display_page_t        page,
                           display_sub_t         target_sub);

void display_update_data(const qmi8658_data_t *imu,
                         const pulse_data_t   *pulse,
                         display_page_t        page,
                         display_sub_t         sub);

void display_draw_page_indicator(display_page_t page, int total);

/* Añade un nuevo punto al buffer circular interno de la gráfica BPM.
 * Llamar desde task_main_cycle cada vez que hay un nuevo BPM válido. */
void display_push_bpm_sample(int bpm);