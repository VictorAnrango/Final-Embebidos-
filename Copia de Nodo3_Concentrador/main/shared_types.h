#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Estructura de datos recibidos por ESPNOW desde Nodo 1
// ============================================================
typedef struct __attribute__((packed)) {
    int   node_id;
    float acc_x, acc_y, acc_z;
    float gyro_x, gyro_y, gyro_z;
    float spo2;
    int   bpm;
    int   bpm_valid;
} nodo1_data_t;

// ============================================================
// Estructura de datos recibidos por ESPNOW desde Nodo 2
// ============================================================
typedef struct __attribute__((packed)) {
    int   node_id;
    float acc_x, acc_y, acc_z;
    float gyro_x, gyro_y, gyro_z;
    float latitud, longitud, altitud, velocidad;
} nodo2_data_t;

// ============================================================
// Datos globales compartidos (definidos en main.c)
// ============================================================
extern nodo1_data_t datos_nodo1;
extern nodo2_data_t datos_nodo2;
extern bool         nodo1_recibido;
extern bool         nodo2_recibido;