#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ============================================================
// Módulo: alerts
//
// Mantiene el estado de las 3 alertas y decide cuándo
// activarlas / desactivarlas según las reglas de tiempo
// y conteo definidas en la especificación.
//
// Reglas resumidas:
//   PULSO      → BPM > bpm_max  durante > 60 s   → apaga con 3 datos buenos
//   OXIGENACIÓN→ SpO2 > spo2_max durante > 120 s  → apaga con 5 datos buenos
//   MOVIMIENTO → mag(acc) > mov_max durante > 30 s→ apaga con 2 datos buenos
// ============================================================

// Inicializa el módulo (crea mutex interno, resetea estado).
void alerts_init(void);

// Evalúa el estado de alertas con los datos más recientes.
// Llamar cada vez que llegue un paquete de cualquier nodo.
// Internamente actualiza OLED y LEDs.
void alerts_evaluate(void);

// Devuelve una copia del estado actual de alertas (thread-safe).
void alerts_get_state(bool *pulso, bool *spo2, bool *movimiento);