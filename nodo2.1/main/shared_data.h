#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Estructura de datos compartida entre todas las tareas
typedef struct {
    int   node_id;
    float acc_x,  acc_y,  acc_z;
    float gyro_x, gyro_y, gyro_z;
    float latitud;
    float longitud;
    float altitud;
    float velocidad;
} nodo2_data_t;

extern nodo2_data_t    shared_data;
extern SemaphoreHandle_t data_mutex;

// Helpers para no olvidar dar/tomar el mutex
static inline void shared_data_lock(void)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
}

static inline void shared_data_unlock(void)
{
    xSemaphoreGive(data_mutex);
}