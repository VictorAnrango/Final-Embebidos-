#pragma once

#include "ssd1306.h"

void oled_init(ssd1306_handle_t handle);
void task_oled(void *pvParameters);