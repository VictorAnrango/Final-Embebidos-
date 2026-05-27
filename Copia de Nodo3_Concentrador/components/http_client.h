#pragma once

// ============================================================
// Módulo: http_client
//
// Encapsula las dos operaciones HTTP del Nodo 3:
//   1. POST  /api/data      → enviar datos de sensores al servidor
//   2. GET   /api/thresholds → obtener umbrales desde el servidor
// ============================================================

// Envía los datos actuales de nodo1 y nodo2 al servidor.
void http_post_data(void);

// Consulta los umbrales al servidor y los almacena internamente.
// Llamar periódicamente desde threshold_task.
void http_fetch_thresholds(void);

// Devuelve los umbrales almacenados (thread-safe).
// Si aún no se consultaron devuelve los valores por defecto.
void http_get_thresholds_values(int   *bpm_max,
                                float *spo2_max,
                                float *mov_max);