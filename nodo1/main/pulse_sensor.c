#include "pulse_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "MAX30100";

/* Registros MAX30100 */
#define MAX30100_REG_INT_STATUS      0x00
#define MAX30100_REG_INT_ENABLE      0x01
#define MAX30100_REG_FIFO_WR_PTR     0x02
#define MAX30100_REG_OVF_COUNTER     0x03
#define MAX30100_REG_FIFO_RD_PTR     0x04
#define MAX30100_REG_FIFO_DATA       0x05
#define MAX30100_REG_MODE_CONFIG     0x06
#define MAX30100_REG_SPO2_CONFIG     0x07
#define MAX30100_REG_LED_CONFIG      0x09

#define MAX30100_MODE_HR_ONLY        0x02
#define MAX30100_MODE_SPO2           0x03
#define MAX30100_RESET               0x40

/* Parámetros de cálculo */
#define SAMPLE_INTERVAL_MS           10
#define FINGER_IR_THRESHOLD          5000

#define DC_ALPHA                     0.05f
#define RR_MIN_MS                    333UL
#define RR_MAX_MS                    2000UL
#define REFRACTORY_MS                300UL
#define PEAK_TIMEOUT_MS              3000UL
#define WARMUP_SAMPLES               50
#define MA_SIZE                      8
#define HISTORY                      8

static bool s_initialized = false;

static float s_ir_dc = 0.0f;
static float s_red_dc = 0.0f;
static float s_ir_ac_level = 0.0f;

static float s_ma_buffer[MA_SIZE];
static int   s_ma_idx = 0;
static int   s_ma_count = 0;
static float s_ma_sum = 0.0f;

static bool     s_peak_detected = false;
static int64_t  s_last_peak_time = 0;
static float    s_last_bpm = 0.0f;
static uint32_t s_intervals[HISTORY];
static int      s_interval_idx = 0;
static int      s_interval_count = 0;
static int      s_warmup = 0;

static uint32_t s_red_min = 0xFFFFFFFF;
static uint32_t s_red_max = 0;
static uint32_t s_ir_min  = 0xFFFFFFFF;
static uint32_t s_ir_max  = 0;
static int      s_spo2_window_count = 0;

static pulse_data_t s_result = {
    .bpm = 0,
    .spo2 = 0,
    .valid = 0
};

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

static inline float fclamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static esp_err_t max30100_write_reg(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX30100_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(MAX30100_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret;
}

static esp_err_t max30100_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX30100_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX30100_I2C_ADDR << 1) | I2C_MASTER_READ, true);

    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }

    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(MAX30100_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret;
}

static esp_err_t max30100_read_fifo(uint16_t *ir, uint16_t *red)
{
    uint8_t buf[4];

    esp_err_t ret = max30100_read_regs(MAX30100_REG_FIFO_DATA, buf, sizeof(buf));
    if (ret != ESP_OK) {
        return ret;
    }

    *ir  = ((uint16_t)buf[0] << 8) | buf[1];
    *red = ((uint16_t)buf[2] << 8) | buf[3];

    return ESP_OK;
}

static void reset_algorithm(void)
{
    s_ir_dc = 0.0f;
    s_red_dc = 0.0f;
    s_ir_ac_level = 0.0f;

    memset(s_ma_buffer, 0, sizeof(s_ma_buffer));
    memset(s_intervals, 0, sizeof(s_intervals));

    s_ma_idx = 0;
    s_ma_count = 0;
    s_ma_sum = 0.0f;

    s_peak_detected = false;
    s_last_peak_time = 0;
    s_last_bpm = 0.0f;
    s_interval_idx = 0;
    s_interval_count = 0;
    s_warmup = 0;

    s_red_min = 0xFFFFFFFF;
    s_red_max = 0;
    s_ir_min = 0xFFFFFFFF;
    s_ir_max = 0;
    s_spo2_window_count = 0;

    s_result.bpm = 0;
    s_result.spo2 = 0;
    s_result.valid = 0;
}

esp_err_t pulse_sensor_init(void)
{
    esp_err_t ret;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MAX30100_SDA_PIN,
        .scl_io_num = MAX30100_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };

    ret = i2c_param_config(MAX30100_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando I2C MAX30100");
        return ret;
    }

    ret = i2c_driver_install(MAX30100_I2C_PORT, conf.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Error instalando driver I2C MAX30100");
        return ret;
    }

    ret = max30100_write_reg(MAX30100_REG_MODE_CONFIG, MAX30100_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No responde MAX30100");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    max30100_write_reg(MAX30100_REG_FIFO_WR_PTR, 0x00);
    max30100_write_reg(MAX30100_REG_OVF_COUNTER, 0x00);
    max30100_write_reg(MAX30100_REG_FIFO_RD_PTR, 0x00);

    max30100_write_reg(MAX30100_REG_SPO2_CONFIG, 0x47);
    max30100_write_reg(MAX30100_REG_LED_CONFIG, 0x77);
    max30100_write_reg(MAX30100_REG_MODE_CONFIG, MAX30100_MODE_SPO2);

    reset_algorithm();
    s_initialized = true;

    ESP_LOGI(TAG, "MAX30100 OK | SDA GPIO%d | SCL GPIO%d",
             MAX30100_SDA_PIN, MAX30100_SCL_PIN);

    return ESP_OK;
}

static void update_spo2(uint16_t ir_raw, uint16_t red_raw)
{
    if (red_raw < s_red_min) s_red_min = red_raw;
    if (red_raw > s_red_max) s_red_max = red_raw;
    if (ir_raw < s_ir_min) s_ir_min = ir_raw;
    if (ir_raw > s_ir_max) s_ir_max = ir_raw;

    s_spo2_window_count++;

    /*
     * Aproximadamente cada 1 segundo si sampleas cada 10 ms.
     */
    if (s_spo2_window_count < 100) {
        return;
    }

    uint32_t red_ac = s_red_max - s_red_min;
    uint32_t ir_ac  = s_ir_max - s_ir_min;

    if (red_ac > 0 && ir_ac > 0 && s_red_dc > 0.0f && s_ir_dc > 0.0f) {
        float ratio = ((float)red_ac / s_red_dc) / ((float)ir_ac / s_ir_dc);

        /*
         * Estimacion basica de SpO2.
         * No es medicion medica, pero sirve para proyecto/prototipo.
         */
        float spo2 = 110.0f - 25.0f * ratio;
        spo2 = fclamp(spo2, 70.0f, 100.0f);

        s_result.spo2 = (int)(spo2 + 0.5f);
    }

    s_red_min = 0xFFFFFFFF;
    s_red_max = 0;
    s_ir_min = 0xFFFFFFFF;
    s_ir_max = 0;
    s_spo2_window_count = 0;
}

void pulse_sensor_sample(void)
{
    if (!s_initialized) {
        return;
    }

    uint16_t ir_raw = 0;
    uint16_t red_raw = 0;

    esp_err_t ret = max30100_read_fifo(&ir_raw, &red_raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Error leyendo FIFO MAX30100");
        return;
    }

    /*
     * Si no hay dedo, la senal IR suele ser baja.
     */
    if (ir_raw < FINGER_IR_THRESHOLD) {
        s_result.bpm = 0;
        s_result.spo2 = 0;
        s_result.valid = 0;

        s_last_bpm = 0.0f;
        s_last_peak_time = 0;
        s_interval_count = 0;
        s_peak_detected = false;
        s_warmup = 0;

        return;
    }

    if (s_ir_dc <= 0.0f) {
        s_ir_dc = ir_raw;
    }

    if (s_red_dc <= 0.0f) {
        s_red_dc = red_raw;
    }

    s_ir_dc  = s_ir_dc  * (1.0f - DC_ALPHA) + ir_raw  * DC_ALPHA;
    s_red_dc = s_red_dc * (1.0f - DC_ALPHA) + red_raw * DC_ALPHA;

    float ir_signal = (float)ir_raw - s_ir_dc;

    s_ma_sum -= s_ma_buffer[s_ma_idx];
    s_ma_buffer[s_ma_idx] = ir_signal;
    s_ma_sum += ir_signal;

    s_ma_idx = (s_ma_idx + 1) % MA_SIZE;

    if (s_ma_count < MA_SIZE) {
        s_ma_count++;
    }

    float smoothed = s_ma_sum / (float)s_ma_count;

    s_ir_ac_level = s_ir_ac_level * 0.95f + fabsf(smoothed) * 0.05f;

    s_warmup++;
    if (s_warmup < WARMUP_SAMPLES) {
        return;
    }

    update_spo2(ir_raw, red_raw);

    float threshold = fclamp(s_ir_ac_level * 0.45f, 20.0f, 2000.0f);
    int64_t now = now_ms();

    if (smoothed > threshold && !s_peak_detected) {
        s_peak_detected = true;

        if (s_last_peak_time > 0) {
            uint32_t interval = (uint32_t)(now - s_last_peak_time);

            if (interval >= RR_MIN_MS && interval <= RR_MAX_MS) {
                s_intervals[s_interval_idx % HISTORY] = interval;
                s_interval_idx++;

                if (s_interval_count < HISTORY) {
                    s_interval_count++;
                }

                float sum = 0.0f;

                for (int i = 0; i < s_interval_count; i++) {
                    sum += s_intervals[i];
                }

                float avg_interval = sum / (float)s_interval_count;
                s_last_bpm = 60000.0f / avg_interval;

                if (s_last_bpm >= 30.0f && s_last_bpm <= 180.0f) {
                    s_result.bpm = (int)(s_last_bpm + 0.5f);
                    s_result.valid = 1;

                    ESP_LOGI(TAG, "BPM=%d | SpO2=%d%% | IR=%u | RED=%u",
                             s_result.bpm,
                             s_result.spo2,
                             ir_raw,
                             red_raw);
                }
            }
        }

        s_last_peak_time = now;
    }
    else if (smoothed < threshold * 0.2f) {
        s_peak_detected = false;
    }

    if (s_last_peak_time > 0 && (now - s_last_peak_time) > PEAK_TIMEOUT_MS) {
        s_result.bpm = 0;
        s_result.valid = 0;
        s_last_bpm = 0.0f;
        s_last_peak_time = 0;
        s_interval_count = 0;
    }
}

pulse_data_t pulse_sensor_get(void)
{
    return s_result;
}