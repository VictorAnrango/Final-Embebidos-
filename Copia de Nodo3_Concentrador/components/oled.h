#pragma once

// ============================================================
// Módulo: oled
// Maneja la pantalla SSD1306 por I2C.
// ============================================================

// Inicializa I2C y la pantalla. Si la pantalla no se encuentra,
// el módulo opera en modo silencioso (sin crash).
void oled_init(void);

// Parámetros de las alertas activas y últimos valores de sensores.
// Si ninguna alerta está activa muestra el estado normal del sistema.
void oled_refresh(bool alerta_pulso,
                  bool alerta_spo2,
                  bool alerta_mov,
                  int  ultimo_bpm,
                  float ultima_spo2);

// Muestra un mensaje libre de 3 líneas (solo para arranque / debug).
void oled_show_message(const char *titulo,
                       const char *linea1,
                       const char *linea2);