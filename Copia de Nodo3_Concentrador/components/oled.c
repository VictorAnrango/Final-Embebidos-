#include "oled.h"

#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "ssd1306.h"

// ============================================================
// Configuración I2C / OLED
// ============================================================

#define I2C_MASTER_SDA_IO  4
#define I2C_MASTER_SCL_IO  15
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define OLED_ADDR          0x3C

static const char    *TAG  = "OLED";
static ssd1306_handle_t oled = NULL;

// ============================================================
// Helpers privados
// ============================================================

// Escribe una línea de 12 px rellenando con espacios para borrar
// texto previo de esa fila (21 chars caben en 128 px a 6 px/char).
static void draw_line(int y, const char *text)
{
    char buf[22];
    snprintf(buf, sizeof(buf), "%-21s", text);
    ssd1306_draw_string(oled, 0, y, (const uint8_t *)buf, 12, true);
}

// ============================================================
// API pública
// ============================================================

void oled_init(void)
{
    // --- I2C ---
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    if (i2c_param_config(I2C_MASTER_NUM, &conf) != ESP_OK ||
        i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0) != ESP_OK)
    {
        ESP_LOGW(TAG, "I2C init falló");
        return;
    }

    // --- OLED ---
    oled = ssd1306_create(I2C_MASTER_NUM, OLED_ADDR);
    if (!oled)
    {
        ESP_LOGW(TAG, "OLED no encontrada, continuando sin pantalla");
        return;
    }

    if (ssd1306_init(oled) != ESP_OK)
    {
        ESP_LOGW(TAG, "OLED init falló");
        oled = NULL;
        return;
    }

    oled_show_message("NODO 3", "Sistema iniciado", "Esperando datos");
    ESP_LOGI(TAG, "OLED iniciada");
}

void oled_show_message(const char *titulo,
                       const char *linea1,
                       const char *linea2)
{
    if (!oled) return;

    ssd1306_clear_screen(oled, false);
    ssd1306_draw_string(oled, 0,  0, (const uint8_t *)titulo, 16, true);
    draw_line(24, linea1);
    draw_line(44, linea2);
    ssd1306_refresh_gram(oled);
}

void oled_refresh(bool  alerta_pulso,
                  bool  alerta_spo2,
                  bool  alerta_mov,
                  int   ultimo_bpm,
                  float ultima_spo2)
{
    if (!oled) return;

    ssd1306_clear_screen(oled, false);

    // --- Sin alertas: mostrar estado normal ---
    if (!alerta_pulso && !alerta_spo2 && !alerta_mov)
    {
        char l1[22], l2[22];
        snprintf(l1, sizeof(l1), "BPM : %d",      ultimo_bpm);
        snprintf(l2, sizeof(l2), "SpO2: %.1f%%",  ultima_spo2);

        ssd1306_draw_string(oled, 0, 0, (const uint8_t *)"== ESTADO ==", 12, true);
        draw_line(16, l1);
        draw_line(30, l2);
        draw_line(46, "Sin alertas");
    }
    else
    {
        // --- Una o más alertas activas ---
        // Construir lista de mensajes activos
        char msgs[3][22];
        int  n = 0;

        if (alerta_pulso)
        {
            snprintf(msgs[n++], 22, "ALERTA PULSO %d", ultimo_bpm);
        }
        if (alerta_spo2)
        {
            snprintf(msgs[n++], 22, "ALERTA OXIGENACION %.0f", ultima_spo2);
        }
        if (alerta_mov)
        {
            snprintf(msgs[n++], 22, "ALTO NIVEL MOVIM");
        }

        // Posiciones Y para 1, 2 o 3 mensajes centrados verticalmente
        // Filas: [n_alertas - 1][índice_mensaje]
        static const int pos_y[3][3] = {
            {26,  0,  0},   // 1 alerta
            {10, 38,  0},   // 2 alertas
            { 0, 22, 44},   // 3 alertas
        };

        for (int i = 0; i < n; i++)
        {
            draw_line(pos_y[n - 1][i], msgs[i]);
        }
    }

    ssd1306_refresh_gram(oled);
}