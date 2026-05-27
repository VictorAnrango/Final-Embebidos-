#pragma once

#include "esp_err.h"
#include "qmi8658.h"
#include "pulse_sensor.h"

// MAC real del Nodo 3
#define NODO3_MAC   {0x08, 0x3A, 0xF2, 0xB7, 0x2F, 0x38}

// Debe ser el SSID de la red donde está conectado el Nodo 3
#define WIFI_SSID_OBJETIVO "VILLA_MARIA"

typedef struct __attribute__((packed)) {

    int node_id;

    float acc_x;
    float acc_y;
    float acc_z;

    float gyro_x;
    float gyro_y;
    float gyro_z;

    float spo2;

    int bpm;
    int bpm_valid;

} nodo1_packet_t;

esp_err_t espnow_init(void);

esp_err_t espnow_send_data(const qmi8658_data_t *imu,
                           const pulse_data_t *pulse);