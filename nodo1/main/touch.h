#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

#define TOUCH_I2C_ADDR   0x15
#define TOUCH_I2C_PORT   I2C_NUM_0
#define TOUCH_INT_PIN    5
#define TOUCH_RST_PIN    13

typedef enum {
    GESTURE_NONE        = 0x00,
    GESTURE_SWIPE_UP    = 0x01,
    GESTURE_SWIPE_DOWN  = 0x02,
    GESTURE_SWIPE_LEFT  = 0x03,
    GESTURE_SWIPE_RIGHT = 0x04,
    GESTURE_TAP         = 0x05,
} touch_gesture_t;

typedef struct {
    touch_gesture_t gesture;
    uint16_t        x;
    uint16_t        y;
    uint8_t         touched;
    bool pressed;
} touch_data_t;

esp_err_t    touch_init(void);
touch_data_t touch_read(void);