#pragma once

// ============================================================
// Módulo: wifi_espnow
//
// Inicializa WiFi en modo STA y ESP-NOW.
// El callback de recepción ESPNOW actualiza los datos globales
// y llama a alerts_evaluate() automáticamente.
// ============================================================

// Inicializa WiFi (bloquea hasta obtener IP) y luego ESP-NOW.
void wifi_espnow_init(void);