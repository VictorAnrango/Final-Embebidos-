#pragma once

// ============================================================
// Módulo: leds
// Controla el LED blanco (normal) y el LED rojo (alerta).
// ============================================================

void leds_init(void);

// Enciende el LED rojo si hay_alerta == true, blanco si false.
void leds_set_alerta(bool hay_alerta);