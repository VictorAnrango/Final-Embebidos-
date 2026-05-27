#pragma once

// ============================================================
// Módulo: tasks
//
// Define y lanza las tareas FreeRTOS del Nodo 3.
//
//   server_task    → POST de datos al servidor cada 10 s
//   threshold_task → GET de umbrales al servidor cada 10 s
// ============================================================

// Crea y registra todas las tareas. Llamar una sola vez desde app_main.
void tasks_start(void);