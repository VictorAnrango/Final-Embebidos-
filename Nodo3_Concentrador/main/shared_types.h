#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ============================================================
// Paquete Nodo 1 — debe ser IDÉNTICO a nodo1_packet_t
// definida en espnow_send.h del Nodo 1
// ============================================================

typedef struct __attribute__((packed)) {
    int   node_id;    // siempre 1
    float acc_x;
    float acc_y;
    float acc_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float spo2;
    int   bpm;
    int   bpm_valid;
} nodo1_data_t;

// ============================================================
// Paquete Nodo 2 — debe ser IDÉNTICO a nodo2_data_t
// definida en shared_data.h del Nodo 2
// ============================================================

typedef struct __attribute__((packed)) {
    int   node_id;    // siempre 2
    float acc_x;
    float acc_y;
    float acc_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float latitud;
    float longitud;
    float altitud;
    float velocidad;
} nodo2_data_t;

// ============================================================
// Variables globales (definidas en wifi_espnow.c)
// ============================================================

extern nodo1_data_t      datos_nodo1;
extern nodo2_data_t      datos_nodo2;
extern bool              nodo1_recibido;
extern bool              nodo2_recibido;
extern SemaphoreHandle_t datos_mutex;