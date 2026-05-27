/*
 * display.c  –  Capa de presentación visual para NODO1
 * Placa: ESP32-S3 Touch LCD 1.28" (GC9A01, 240×240, SPI)
 * -------------------------------------------------------
 * Características implementadas
 *   • Sin parpadeo: refresco parcial por zonas; sin fill_rect(0,0,240,240)
 *   • Animación de scroll vertical suavizada (interpolación lineal)
 *   • Fuente bitmap mejorada (8×12, más legible y proporcional)
 *   • Fuente numérica grande para BPM (segmentos de 7-segmentos estilo)
 *   • Dashboard premium con coronas concéntricas y sombras simuladas
 *   • Buffer circular de 64 muestras para gráfica ECG en tiempo real
 *   • Arcos circulares de progreso para SpO2
 *   • Barras de nivel dinámicas para acelerómetro y giroscopio
 *   • Compatible 100 % con qmi8658_data_t y pulse_data_t existentes
 *   • Mantiene intacta la inicialización SPI (pines, host, frecuencia)
 */

#include "display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static const char *TAG = "DISPLAY";

/* ═══════════════════════════════════════════════════════════════════════
 *  PINES Y DIMENSIONES  (sin cambios respecto al original)
 * ═══════════════════════════════════════════════════════════════════════*/
#define LCD_MOSI   11
#define LCD_CLK    10
#define LCD_CS      9
#define LCD_DC      8
#define LCD_RST    14
#define LCD_BL      2
#define LCD_W      240
#define LCD_H      240

/* ═══════════════════════════════════════════════════════════════════════
 *  COLORES  RGB565
 * ═══════════════════════════════════════════════════════════════════════*/
#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

#define BLACK    0x0000
#define WHITE    0xFFFF
#define CYAN     RGB565(0,220,255)
#define GREEN    RGB565(0,220,80)
#define YELLOW   RGB565(255,210,0)
#define RED      RGB565(220,40,40)
#define DKRED    RGB565(120,10,10)
#define GREY     RGB565(110,110,110)
#define LTGREY   RGB565(180,180,180)
#define DKGREY   RGB565(28,28,36)
#define DKGREY2  RGB565(18,18,24)
#define BLUE     RGB565(30,100,255)
#define ORANGE   RGB565(255,120,0)
#define PURPLE   RGB565(130,40,210)
#define PINK     RGB565(240,40,150)
#define ACCENT   RGB565(220,40,40)   /* acento principal = rojo */
#define TEAL     RGB565(0,180,160)

/* ═══════════════════════════════════════════════════════════════════════
 *  SPI
 * ═══════════════════════════════════════════════════════════════════════*/
static spi_device_handle_t spi_dev;

static void lcd_cmd(uint8_t cmd) {
    gpio_set_level(LCD_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_transmit(spi_dev, &t);
}
static void lcd_data(const uint8_t *data, int len) {
    if (len == 0) return;
    gpio_set_level(LCD_DC, 1);
    spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = data };
    spi_device_transmit(spi_dev, &t);
}
static void lcd_data_byte(uint8_t d) { lcd_data(&d, 1); }

/* ═══════════════════════════════════════════════════════════════════════
 *  SECUENCIA DE INICIALIZACIÓN GC9A01  (sin cambios)
 * ═══════════════════════════════════════════════════════════════════════*/
static void gc9a01_init_seq(void) {
    gpio_set_level(LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0xEF);
    lcd_cmd(0xEB); lcd_data_byte(0x14);
    lcd_cmd(0xFE);
    lcd_cmd(0xEF);
    lcd_cmd(0xEB); lcd_data_byte(0x14);
    lcd_cmd(0x84); lcd_data_byte(0x40);
    lcd_cmd(0x85); lcd_data_byte(0xFF);
    lcd_cmd(0x86); lcd_data_byte(0xFF);
    lcd_cmd(0x87); lcd_data_byte(0xFF);
    lcd_cmd(0x88); lcd_data_byte(0x0A);
    lcd_cmd(0x89); lcd_data_byte(0x21);
    lcd_cmd(0x8A); lcd_data_byte(0x00);
    lcd_cmd(0x8B); lcd_data_byte(0x80);
    lcd_cmd(0x8C); lcd_data_byte(0x01);
    lcd_cmd(0x8D); lcd_data_byte(0x01);
    lcd_cmd(0x8E); lcd_data_byte(0xFF);
    lcd_cmd(0x8F); lcd_data_byte(0xFF);
    lcd_cmd(0xB6); lcd_data_byte(0x00); lcd_data_byte(0x00);
    lcd_cmd(0x36); lcd_data_byte(0x48);
    lcd_cmd(0x3A); lcd_data_byte(0x05);
    lcd_cmd(0x90); lcd_data_byte(0x08); lcd_data_byte(0x08);
                   lcd_data_byte(0x08); lcd_data_byte(0x08);
    lcd_cmd(0xBD); lcd_data_byte(0x06);
    lcd_cmd(0xBC); lcd_data_byte(0x00);
    lcd_cmd(0xFF); lcd_data_byte(0x60); lcd_data_byte(0x01); lcd_data_byte(0x04);
    lcd_cmd(0xC3); lcd_data_byte(0x13);
    lcd_cmd(0xC4); lcd_data_byte(0x13);
    lcd_cmd(0xC9); lcd_data_byte(0x22);
    lcd_cmd(0xBE); lcd_data_byte(0x11);
    lcd_cmd(0xE1); lcd_data_byte(0x10); lcd_data_byte(0x0E);
    lcd_cmd(0xDF); lcd_data_byte(0x21); lcd_data_byte(0x0C); lcd_data_byte(0x02);
    lcd_cmd(0xF0); lcd_data_byte(0x45); lcd_data_byte(0x09); lcd_data_byte(0x08);
                   lcd_data_byte(0x08); lcd_data_byte(0x26); lcd_data_byte(0x2A);
    lcd_cmd(0xF1); lcd_data_byte(0x43); lcd_data_byte(0x70); lcd_data_byte(0x72);
                   lcd_data_byte(0x36); lcd_data_byte(0x37); lcd_data_byte(0x6F);
    lcd_cmd(0xF2); lcd_data_byte(0x45); lcd_data_byte(0x09); lcd_data_byte(0x08);
                   lcd_data_byte(0x08); lcd_data_byte(0x26); lcd_data_byte(0x2A);
    lcd_cmd(0xF3); lcd_data_byte(0x43); lcd_data_byte(0x70); lcd_data_byte(0x72);
                   lcd_data_byte(0x36); lcd_data_byte(0x37); lcd_data_byte(0x6F);
    lcd_cmd(0xED); lcd_data_byte(0x1B); lcd_data_byte(0x0B);
    lcd_cmd(0xAE); lcd_data_byte(0x77);
    lcd_cmd(0xCD); lcd_data_byte(0x63);
    lcd_cmd(0x70); lcd_data_byte(0x07); lcd_data_byte(0x07); lcd_data_byte(0x04);
                   lcd_data_byte(0x0E); lcd_data_byte(0x0F); lcd_data_byte(0x09);
                   lcd_data_byte(0x07); lcd_data_byte(0x08); lcd_data_byte(0x03);
    lcd_cmd(0xE8); lcd_data_byte(0x34);
    lcd_cmd(0x62); lcd_data_byte(0x18); lcd_data_byte(0x0D); lcd_data_byte(0x71);
                   lcd_data_byte(0xED); lcd_data_byte(0x70); lcd_data_byte(0x70);
                   lcd_data_byte(0x18); lcd_data_byte(0x0F); lcd_data_byte(0x71);
                   lcd_data_byte(0xEF); lcd_data_byte(0x70); lcd_data_byte(0x70);
    lcd_cmd(0x63); lcd_data_byte(0x18); lcd_data_byte(0x11); lcd_data_byte(0x71);
                   lcd_data_byte(0xF1); lcd_data_byte(0x70); lcd_data_byte(0x70);
                   lcd_data_byte(0x18); lcd_data_byte(0x13); lcd_data_byte(0x71);
                   lcd_data_byte(0xF3); lcd_data_byte(0x70); lcd_data_byte(0x70);
    lcd_cmd(0x64); lcd_data_byte(0x28); lcd_data_byte(0x29); lcd_data_byte(0xF1);
                   lcd_data_byte(0x01); lcd_data_byte(0xF1); lcd_data_byte(0x00);
                   lcd_data_byte(0x07);
    lcd_cmd(0x66); lcd_data_byte(0x3C); lcd_data_byte(0x00); lcd_data_byte(0xCD);
                   lcd_data_byte(0x67); lcd_data_byte(0x45); lcd_data_byte(0x45);
                   lcd_data_byte(0x10); lcd_data_byte(0x00); lcd_data_byte(0x00);
                   lcd_data_byte(0x00);
    lcd_cmd(0x67); lcd_data_byte(0x00); lcd_data_byte(0x3C); lcd_data_byte(0x00);
                   lcd_data_byte(0x00); lcd_data_byte(0x00); lcd_data_byte(0x01);
                   lcd_data_byte(0x54); lcd_data_byte(0x10); lcd_data_byte(0x32);
                   lcd_data_byte(0x98);
    lcd_cmd(0x74); lcd_data_byte(0x10); lcd_data_byte(0x85); lcd_data_byte(0x80);
                   lcd_data_byte(0x00); lcd_data_byte(0x00); lcd_data_byte(0x4E);
                   lcd_data_byte(0x00);
    lcd_cmd(0x98); lcd_data_byte(0x3E); lcd_data_byte(0x07);
    lcd_cmd(0x35);
    lcd_cmd(0x21);
    lcd_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}

/* ═══════════════════════════════════════════════════════════════════════
 *  PRIMITIVAS GRÁFICAS BÁSICAS
 * ═══════════════════════════════════════════════════════════════════════*/

/* Buffer de línea compartido para transfers SPI (480 bytes = 1 línea RGB565) */
static uint8_t g_line_buf[LCD_W * 2];

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t d[4];
    lcd_cmd(0x2A);
    d[0]=x0>>8; d[1]=x0; d[2]=x1>>8; d[3]=x1; lcd_data(d,4);
    lcd_cmd(0x2B);
    d[0]=y0>>8; d[1]=y0; d[2]=y1>>8; d[3]=y1; lcd_data(d,4);
    lcd_cmd(0x2C);
}

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (w == 0 || h == 0) return;
    if (x >= LCD_W || y >= LCD_H) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    set_window(x, y, x+w-1, y+h-1);
    gpio_set_level(LCD_DC, 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    int blen = w * 2;
    for (int i = 0; i < blen; i+=2) { g_line_buf[i]=hi; g_line_buf[i+1]=lo; }
    spi_transaction_t t = { .length=(size_t)blen*8, .tx_buffer=g_line_buf };
    for (int j = 0; j < h; j++) spi_device_transmit(spi_dev, &t);
}

/* Dibuja un solo píxel (usa fill_rect 1×1 para no duplicar lógica SPI) */
static inline void put_pixel(uint16_t x, uint16_t y, uint16_t color) {
    fill_rect(x, y, 1, 1, color);
}

/* Círculo relleno mediante escaneo por línea */
static void fill_circle(int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int xl = 0;
        while (xl*xl + dy*dy <= r*r) xl++;
        xl--;
        int px = cx - xl, py = cy + dy;
        if (py < 0 || py >= LCD_H) continue;
        if (px < 0) px = 0;
        int pw = xl*2+1;
        if (px + pw > LCD_W) pw = LCD_W - px;
        fill_rect((uint16_t)px, (uint16_t)py, (uint16_t)pw, 1, color);
    }
}

/* Arco circular (ángulos en grados, 0=arriba, sentido horario)
 * grosor = t píxeles, dibujo por pasos angulares de 1 grado */
static void draw_arc(int cx, int cy, int r, int t,
                     int ang_start, int ang_end, uint16_t color) {
    const float PI = 3.14159265f;
    for (int a = ang_start; a <= ang_end; a++) {
        float rad = (a - 90) * PI / 180.0f;
        for (int rr = r - t + 1; rr <= r; rr++) {
            int x = cx + (int)(rr * cosf(rad));
            int y = cy + (int)(rr * sinf(rad));
            if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H)
                put_pixel((uint16_t)x, (uint16_t)y, color);
        }
    }
}

/* Línea de Bresenham */
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1-x0), dy = abs(y1-y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        if (x0>=0 && x0<LCD_W && y0>=0 && y0<LCD_H)
            put_pixel((uint16_t)x0,(uint16_t)y0,color);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  FUENTE MEJORADA  8×12 px  (estilo condensed sans-serif)
 *  Conjunto: 0-9, A-Z, espacio, punto, %, +, -, :, /
 *  Cada carácter = 8 bytes (1 byte por fila, 8 filas útiles de 12 totales)
 * ═══════════════════════════════════════════════════════════════════════*/
/* Fuente 6 columnas × 10 filas almacenada en 10 bytes por glifo */
static const uint8_t FONT6x10[][10] = {
    /* 0 */ {0x3C,0x66,0x6E,0x76,0x66,0x66,0x66,0x66,0x3C,0x00},
    /* 1 */ {0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    /* 2 */ {0x3C,0x66,0x06,0x0C,0x18,0x30,0x60,0x66,0x7E,0x00},
    /* 3 */ {0x3C,0x66,0x06,0x1C,0x06,0x06,0x06,0x66,0x3C,0x00},
    /* 4 */ {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x0C,0x0C,0x00},
    /* 5 */ {0x7E,0x60,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C,0x00},
    /* 6 */ {0x1C,0x30,0x60,0x7C,0x66,0x66,0x66,0x66,0x3C,0x00},
    /* 7 */ {0x7E,0x06,0x0C,0x18,0x18,0x30,0x30,0x30,0x30,0x00},
    /* 8 */ {0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x66,0x3C,0x00},
    /* 9 */ {0x3C,0x66,0x66,0x66,0x3E,0x06,0x0C,0x18,0x70,0x00},
    /* A */ {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x00},
    /* B */ {0x7C,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0x7C,0x00},
    /* C */ {0x3C,0x66,0x60,0x60,0x60,0x60,0x60,0x66,0x3C,0x00},
    /* D */ {0x78,0x6C,0x66,0x66,0x66,0x66,0x66,0x6C,0x78,0x00},
    /* E */ {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x7E,0x00},
    /* F */ {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x60,0x00},
    /* G */ {0x3C,0x66,0x60,0x60,0x6E,0x66,0x66,0x66,0x3C,0x00},
    /* H */ {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x66,0x00},
    /* I */ {0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    /* J */ {0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00},
    /* K */ {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x66,0x66,0x00},
    /* L */ {0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    /* M */ {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x63,0x63,0x00},
    /* N */ {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x66,0x66,0x00},
    /* O */ {0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    /* P */ {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x60,0x00},
    /* Q */ {0x3C,0x66,0x66,0x66,0x66,0x76,0x6C,0x66,0x3B,0x00},
    /* R */ {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x66,0x66,0x00},
    /* S */ {0x3C,0x66,0x60,0x30,0x18,0x0C,0x06,0x66,0x3C,0x00},
    /* T */ {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    /* U */ {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    /* V */ {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    /* W */ {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x63,0x63,0x00},
    /* X */ {0x66,0x66,0x3C,0x18,0x18,0x3C,0x66,0x66,0x66,0x00},
    /* Y */ {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0x00},
    /* Z */ {0x7E,0x06,0x0C,0x18,0x18,0x30,0x60,0x60,0x7E,0x00},
    /* ' '*/  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    /* '%' */ {0x62,0x66,0x0C,0x18,0x30,0x66,0x46,0x00,0x00,0x00},
    /* '+' */ {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00},
    /* '-' */ {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00},
    /* ':' */ {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
    /* '/' */ {0x02,0x06,0x0C,0x18,0x30,0x60,0x40,0x00,0x00,0x00},
};
#define FONT_W  6
#define FONT_H  10

static int char_to_idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c == ' ') return 36;
    if (c == '.') return 37;
    if (c == '%') return 38;
    if (c == '+') return 39;
    if (c == '-') return 40;
    if (c == ':') return 41;
    if (c == '/') return 42;
    return 36;
}

/*
 * Dibuja un carácter con escala entera.
 * scale=1 → 6×10 px, scale=2 → 12×20 px, etc.
 * Usa fill_rect por bit para no parchear el bus con 1 píxel a la vez
 * cuando scale>1.
 */
static void draw_char(uint16_t x, uint16_t y, char c,
                      uint16_t fg, uint16_t bg, int scale) {
    int idx = char_to_idx(c);
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = FONT6x10[idx][row];
        for (int col = 0; col < FONT_W; col++) {
            uint16_t color = (bits & (0x80 >> col)) ? fg : bg;
            fill_rect(x + col*scale, y + row*scale, (uint16_t)scale, (uint16_t)scale, color);
        }
    }
}

static int str_width(const char *s, int scale) {
    int len = 0;
    while (*s++) len++;
    return len * (FONT_W + 1) * scale;
}

static void draw_string(uint16_t x, uint16_t y, const char *s,
                        uint16_t fg, uint16_t bg, int scale) {
    while (*s) {
        draw_char(x, y, *s, fg, bg, scale);
        x += (uint16_t)((FONT_W + 1) * scale);
        s++;
    }
}

static void draw_string_centered(uint16_t cx, uint16_t y, const char *s,
                                 uint16_t fg, uint16_t bg, int scale) {
    int w = str_width(s, scale);
    int x = (int)cx - w / 2;
    if (x < 0) x = 0;
    draw_string((uint16_t)x, y, s, fg, bg, scale);
}

/* Limpia el fondo de un string centrado antes de actualizarlo */
static void clear_string_centered(uint16_t cx, uint16_t y, int max_chars,
                                  uint16_t bg, int scale) {
    int w = max_chars * (FONT_W + 1) * scale;
    int x = (int)cx - w / 2;
    if (x < 0) x = 0;
    fill_rect((uint16_t)x, y, (uint16_t)w, (uint16_t)(FONT_H * scale), bg);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  FUENTE NUMÉRICA GRANDE (segmentos tipo LCD, 3 columnas de 5 px)
 *  Para mostrar BPM en tamaño grande sin escalar la fuente base.
 *  Cada dígito dibujado con rectángulos de segmento.
 * ═══════════════════════════════════════════════════════════════════════*/
/*
 * Segmentos:   _
 *             |_|
 *             |_|
 * Índices:  A(top) B(top-right) C(bot-right) D(bot) E(bot-left) F(top-left) G(mid)
 * Bits:     A=0x01 B=0x02 C=0x04 D=0x08 E=0x10 F=0x20 G=0x40
 */
static const uint8_t SEG7[10] = {
    /* 0 */ 0x3F, /* 1 */ 0x06, /* 2 */ 0x5B, /* 3 */ 0x4F,
    /* 4 */ 0x66, /* 5 */ 0x6D, /* 6 */ 0x7D, /* 7 */ 0x07,
    /* 8 */ 0x7F, /* 9 */ 0x6F,
};

/* Dibuja un dígito grande. sw=anchura segmento, sh=altura segmento */
static void draw_big_digit(int x, int y, int digit, uint16_t color, uint16_t bg) {
    const int sw = 22;  /* ancho total del dígito */
    const int sh = 4;   /* grosor del segmento */
    const int hh = 20;  /* mitad de altura */
    uint8_t seg = (digit >= 0 && digit <= 9) ? SEG7[digit] : 0;

    /* Limpia área del dígito */
    fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)sw, (uint16_t)(hh*2+sh), bg);

    /* A top */    if (seg&0x01) fill_rect(x+sh,   y,          sw-sh*2, sh,   color);
    /* B top-R */  if (seg&0x02) fill_rect(x+sw-sh, y+sh,      sh, hh-sh,    color);
    /* C bot-R */  if (seg&0x04) fill_rect(x+sw-sh, y+hh,      sh, hh-sh,    color);
    /* D bot */    if (seg&0x08) fill_rect(x+sh,   y+hh*2,     sw-sh*2, sh,   color);
    /* E bot-L */  if (seg&0x10) fill_rect(x,       y+hh,      sh, hh-sh,    color);
    /* F top-L */  if (seg&0x20) fill_rect(x,       y+sh,      sh, hh-sh,    color);
    /* G mid */    if (seg&0x40) fill_rect(x+sh,   y+hh-sh/2,  sw-sh*2, sh,   color);
}

/* Dibuja un número de hasta 3 dígitos centrado en cx */
static void draw_big_number(uint16_t cx, uint16_t y, int val, int digits,
                             uint16_t color, uint16_t bg) {
    const int dw = 26;   /* ancho por dígito + gap */
    int total_w = digits * dw;
    int x = (int)cx - total_w / 2;
    /* Limpia zona */
    fill_rect((uint16_t)x, y, (uint16_t)total_w, 45, bg);
    /* Descompone dígitos */
    int tmp = val;
    int d[3] = {-1,-1,-1};
    if (digits >= 3) { d[0] = tmp/100; tmp %= 100; }
    if (digits >= 2) { d[digits==3?1:0] = tmp/10;  tmp %= 10; }
    d[digits-1] = tmp;
    for (int i = 0; i < digits; i++) {
        if (d[i] >= 0)
            draw_big_digit(x + i*dw, y, d[i], color, bg);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  ICONOS
 * ═══════════════════════════════════════════════════════════════════════*/
static void draw_heart_icon(int cx, int cy, uint16_t color) {
    fill_circle(cx-5, cy-4, 5, color);
    fill_circle(cx+5, cy-4, 5, color);
    fill_rect(cx-9, cy-2, 18, 8, color);
    fill_rect(cx-6, cy+6, 12, 4, color);
    fill_rect(cx-3, cy+10, 6, 3, color);
}
static void draw_clock_icon(int cx, int cy, uint16_t color) {
    fill_circle(cx, cy, 8, color);
    fill_circle(cx, cy, 5, BLACK);
    fill_rect(cx, cy-4, 2, 5, color);
    fill_rect(cx, cy, 4, 2, color);
}
static void draw_settings_icon(int cx, int cy, uint16_t color) {
    fill_circle(cx, cy, 8, color);
    fill_circle(cx, cy, 3, BLACK);
    fill_rect(cx-1, cy-12, 3, 5, color);
    fill_rect(cx-1, cy+7,  3, 5, color);
    fill_rect(cx-12, cy-1, 5, 3, color);
    fill_rect(cx+7,  cy-1, 5, 3, color);
}
static void draw_music_icon(int cx, int cy, uint16_t color) {
    fill_rect(cx+2, cy-9, 3, 14, color);
    fill_rect(cx+2, cy-9, 9, 3,  color);
    fill_circle(cx-2, cy+6, 5, color);
}
static void draw_plus_icon(int cx, int cy, uint16_t color) {
    fill_rect(cx-6, cy-1, 13, 3, color);
    fill_rect(cx-1, cy-6, 3,  13, color);
}
static void draw_activity_icon(int cx, int cy, uint16_t color) {
    /* Onda ECG simplificada */
    draw_line(cx-10, cy,    cx-5,  cy,    color);
    draw_line(cx-5,  cy,    cx-2,  cy-8,  color);
    draw_line(cx-2,  cy-8,  cx+2,  cy+8,  color);
    draw_line(cx+2,  cy+8,  cx+5,  cy,    color);
    draw_line(cx+5,  cy,    cx+10, cy,    color);
}
static void draw_message_icon(int cx, int cy, uint16_t color) {
    fill_rect(cx-9, cy-6, 18, 12, color);
    fill_rect(cx-4, cy+5, 5,  4,  color);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  BUFFER CIRCULAR BPM  (gráfica ECG)
 * ═══════════════════════════════════════════════════════════════════════*/
#define BPM_BUF_SIZE  64
static int  g_bpm_buf[BPM_BUF_SIZE] = {0};
static int  g_bpm_head = 0;
static int  g_bpm_count = 0;

void display_push_bpm_sample(int bpm) {
    g_bpm_buf[g_bpm_head] = bpm;
    g_bpm_head = (g_bpm_head + 1) % BPM_BUF_SIZE;
    if (g_bpm_count < BPM_BUF_SIZE) g_bpm_count++;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  GRÁFICA BPM EN TIEMPO REAL
 *  Zona: x=[30..210], y=[top..top+h-1]
 * ═══════════════════════════════════════════════════════════════════════*/
static void draw_bpm_graph(uint16_t gx, uint16_t gy, uint16_t gw, uint16_t gh,
                           uint16_t line_color, uint16_t bg_color) {
    /* Fondo de la gráfica */
    fill_rect(gx, gy, gw, gh, bg_color);

    /* Línea base central (tenue) */
    int base_y = gy + gh/2;
    fill_rect(gx, (uint16_t)base_y, gw, 1, DKGREY);

    if (g_bpm_count < 2) return;

    /* Busca min/max en el buffer para escalar la amplitud */
    int bmin = 999, bmax = 0;
    for (int i = 0; i < g_bpm_count; i++) {
        int v = g_bpm_buf[i];
        if (v < bmin) bmin = v;
        if (v > bmax) bmax = v;
    }
    int brange = bmax - bmin;
    if (brange < 10) brange = 10;  /* mínimo rango para que se vea algo */

    /* Dibuja puntos del buffer de más antiguo a más nuevo */
    int n = g_bpm_count;
    int tail = (g_bpm_head - n + BPM_BUF_SIZE) % BPM_BUF_SIZE;

    int prev_px = -1, prev_py = -1;
    for (int i = 0; i < n; i++) {
        int v = g_bpm_buf[(tail + i) % BPM_BUF_SIZE];
        int px = gx + (int)((long)i * (gw - 1) / (n - 1));
        int py = gy + gh - 1 - (int)((long)(v - bmin) * (gh - 2) / brange);
        if (py < (int)gy)       py = gy;
        if (py >= (int)(gy+gh)) py = gy + gh - 1;
        if (prev_px >= 0)
            draw_line(prev_px, prev_py, px, py, line_color);
        prev_px = px; prev_py = py;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  BARRAS DE NIVEL PARA IMU
 *  val en [-max_val..+max_val], dibuja barra horizontal centrada
 * ═══════════════════════════════════════════════════════════════════════*/
static void draw_level_bar(uint16_t bx, uint16_t by, uint16_t bw, uint16_t bh,
                           float val, float max_val, uint16_t color) {
    /* Fondo de la barra */
    fill_rect(bx, by, bw, bh, DKGREY2);
    /* Línea central */
    uint16_t mid = bx + bw/2;
    fill_rect(mid, by, 1, bh, DKGREY);
    /* Longitud proporcional */
    if (val > max_val)  val = max_val;
    if (val < -max_val) val = -max_val;
    int half = (int)((fabsf(val) / max_val) * (bw/2));
    if (half < 1) half = 1;
    if (val >= 0)
        fill_rect(mid, by+1, (uint16_t)half, bh-2, color);
    else
        fill_rect((uint16_t)(mid - half), by+1, (uint16_t)half, bh-2, color);
}

/* Barra de progreso circular (arco) para SpO2 */
static void draw_progress_arc(int cx, int cy, int r, int t, int pct,
                              uint16_t fg_color, uint16_t bg_color) {
    /* Fondo del arco (gris) */
    draw_arc(cx, cy, r, t, 0, 359, DKGREY);
    /* Arco de progreso */
    int end_ang = (int)((long)pct * 360 / 100);
    if (end_ang > 0)
        draw_arc(cx, cy, r, t, 0, end_ang-1, fg_color);
    (void)bg_color;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  BOTÓN DE REGRESO
 * ═══════════════════════════════════════════════════════════════════════*/
static void draw_back_button(void) {
    fill_circle(28, 28, 17, DKGREY);
    fill_circle(28, 28, 15, RGB565(40,40,52));
    /* Flecha < */
    fill_rect(20, 27, 14, 2, LTGREY);
    fill_rect(20, 24, 5, 2,  LTGREY);
    fill_rect(20, 30, 5, 2,  LTGREY);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  INDICADOR DE PÁGINA (puntos inferiores)
 * ═══════════════════════════════════════════════════════════════════════*/
void display_draw_page_indicator(display_page_t page, int total) {
    uint16_t cx = LCD_W / 2;
    uint16_t y  = 225;
    int dot_r   = 3;
    int spacing = 14;
    int start_x = (int)cx - ((total - 1) * spacing) / 2;
    for (int i = 0; i < total; i++) {
        uint16_t dx = (uint16_t)(start_x + i * spacing);
        /* Borra posición anterior */
        fill_circle(dx, y, dot_r+1, BLACK);
        uint16_t color = (i == (int)page) ? WHITE : GREY;
        fill_circle(dx, y, dot_r, color);
        if (i == (int)page) {  /* Dot activo más grande */
            fill_circle(dx, y, dot_r-1, ACCENT);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  INDICADOR DE SCROLL (flecha arriba/abajo en el borde)
 * ═══════════════════════════════════════════════════════════════════════*/
static void draw_scroll_hint(display_sub_t sub) {
    /* Pequeña flecha en x=215, y=120 indicando dirección disponible */
    fill_rect(210, 110, 14, 20, BLACK);  /* borra zona */
    if (sub == SUB_TOP) {
        /* Flecha hacia arriba (hay contenido arriba no) → flecha abajo */
        fill_rect(214, 120, 6, 2, GREY);
        fill_rect(213, 122, 8, 2, GREY);
        fill_rect(214, 124, 6, 2, GREY);
        fill_rect(215, 126, 4, 2, GREY);
        fill_rect(216, 128, 2, 2, GREY);
    } else {
        /* Flecha hacia arriba (hay contenido abajo no) → flecha arriba */
        fill_rect(216, 112, 2, 2, GREY);
        fill_rect(215, 114, 4, 2, GREY);
        fill_rect(214, 116, 6, 2, GREY);
        fill_rect(213, 118, 8, 2, GREY);
        fill_rect(214, 120, 6, 2, GREY);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  PÁGINA IMU – SECCIÓN SUPERIOR (Acelerómetro)
 * ═══════════════════════════════════════════════════════════════════════*/
static void draw_imu_top_static(void) {
    uint16_t cx = LCD_W / 2;
    /* Fondo */
    fill_rect(0, 0, LCD_W, LCD_H, BLACK);

    draw_back_button();

    /* Título "MOVIMIENTO" en CYAN, escala 1 */
    draw_string_centered(cx, 30, "MOVIMIENTO", CYAN, BLACK, 1);

    /* Separador curvo superior */
    draw_arc(cx, -20, 80, 2, 60, 120, CYAN);

    /* Etiquetas ejes acelerómetro */
    draw_string_centered(50, 74, "X", CYAN, BLACK, 2);
    draw_string_centered(50, 120, "Y", CYAN, BLACK, 2);
    draw_string_centered(50, 166, "Z", CYAN, BLACK, 2);

    /* Etiqueta sección */
    draw_string_centered(cx, 45, "ACELEROMETRO g", GREY, BLACK, 1);
    fill_rect(30, 58, 180, 1, DKGREY);
}

static void draw_imu_bot_static(void) {
    uint16_t cx = LCD_W / 2;
    fill_rect(0, 0, LCD_W, LCD_H, BLACK);

    draw_back_button();

    draw_string_centered(cx, 30, "MOVIMIENTO", CYAN, BLACK, 1);
    draw_arc(cx, -20, 80, 2, 60, 120, GREEN);

    draw_string_centered(50, 74, "X", GREEN, BLACK, 2);
    draw_string_centered(50, 120, "Y", GREEN, BLACK, 2);
    draw_string_centered(50, 166, "Z", GREEN, BLACK, 2);

    draw_string_centered(cx, 45, "GIROSCOPIO dps", GREY, BLACK, 1);
    fill_rect(30, 58, 180, 1, DKGREY);
}

/* Actualiza solo los valores numéricos y barras de la sección IMU */
static void update_imu_top_values(const qmi8658_data_t *imu) {
    char buf[16];
    uint16_t bx = 65, bw = 140, bh = 7;

    /* ACC X */
    fill_rect(65, 72, 150, 11, BLACK);
    snprintf(buf, sizeof(buf), "%+.2f", imu->acc_x);
    draw_string(65, 72, buf, WHITE, BLACK, 2);
    draw_level_bar(bx, 90, bw, bh, imu->acc_x, 2.0f, CYAN);

    /* ACC Y */
    fill_rect(65, 117, 150, 11, BLACK);
    snprintf(buf, sizeof(buf), "%+.2f", imu->acc_y);
    draw_string(65, 117, buf, WHITE, BLACK, 2);
    draw_level_bar(bx, 136, bw, bh, imu->acc_y, 2.0f, CYAN);

    /* ACC Z */
    fill_rect(65, 164, 150, 11, BLACK);
    snprintf(buf, sizeof(buf), "%+.2f", imu->acc_z);
    draw_string(65, 164, buf, WHITE, BLACK, 2);
    draw_level_bar(bx, 182, bw, bh, imu->acc_z, 2.0f, CYAN);
}

static void update_imu_bot_values(const qmi8658_data_t *imu) {
    char buf[16];
    uint16_t bx = 65, bw = 140, bh = 7;

    /* GYR X */
    fill_rect(65, 72, 150, 11, BLACK);
    snprintf(buf, sizeof(buf), "%+.1f", imu->gyro_x);
    draw_string(65, 72, buf, WHITE, BLACK, 2);
    draw_level_bar(bx, 90, bw, bh, imu->gyro_x, 250.0f, GREEN);

    /* GYR Y */
    fill_rect(65, 117, 150, 11, BLACK);
    snprintf(buf, sizeof(buf), "%+.1f", imu->gyro_y);
    draw_string(65, 117, buf, WHITE, BLACK, 2);
    draw_level_bar(bx, 136, bw, bh, imu->gyro_y, 250.0f, GREEN);

    /* GYR Z */
    fill_rect(65, 164, 150, 11, BLACK);
    snprintf(buf, sizeof(buf), "%+.1f", imu->gyro_z);
    draw_string(65, 164, buf, WHITE, BLACK, 2);
    draw_level_bar(bx, 182, bw, bh, imu->gyro_z, 250.0f, GREEN);
}
//////////////////////////////////////////////////////////////////////////////////////PARTE DE EL SENSOR DE RITMO CARDIACO 
/* ═══════════════════════════════════════════════════════════════════════
 *  PÁGINA SALUD – SECCIÓN SUPERIOR (BPM + gráfica)
 * ═══════════════════════════════════════════════════════════════════════*/
static void draw_pulse_top_static(void) {
    uint16_t cx = LCD_W / 2;
    fill_rect(0, 0, LCD_W, LCD_H, BLACK);
 
    draw_back_button();
 
    draw_string_centered(cx, 10, "SALUD", RED, BLACK, 1);
    draw_arc(cx, -20, 80, 2, 60, 120, RED);
 
    /* Icono corazón pequeño junto al título */
    draw_heart_icon(cx + 38, 15, RED);
 
    /* Etiqueta BPM */
    draw_string_centered(cx, 38, "FREC CARDIACA", GREY, BLACK, 1);
    fill_rect(30, 50, 180, 1, DKGREY);
 
    /* Marco de la gráfica */
    fill_rect(28, 130, 184, 1, DKGREY);   /* borde sup */
    fill_rect(28, 188, 184, 1, DKGREY);   /* borde inf */
    fill_rect(28, 130, 1,   58, DKGREY);  /* borde izq */
    fill_rect(212,130, 1,   58, DKGREY);  /* borde der */
    /* Etiqueta gráfica */
    draw_string(30, 133, "BPM", DKGREY, BLACK, 1);
}
 
static void draw_pulse_bot_static(void) {
    uint16_t cx = LCD_W / 2;
    fill_rect(0, 0, LCD_W, LCD_H, BLACK);
 
    draw_back_button();
 
    draw_string_centered(cx, 10, "SALUD", RED, BLACK, 1);
    draw_arc(cx, -20, 80, 2, 60, 120, TEAL);
 
    draw_string_centered(cx, 38, "OXIMETRIA SpO2", GREY, BLACK, 1);
    fill_rect(30, 50, 180, 1, DKGREY);
 
    /* Arco de fondo SpO2  — subido a cy=110 para centrarlo mejor */
    draw_arc(cx, 110, 60, 10, 0, 359, DKGREY);
 
    /* Etiqueta debajo del arco */
    draw_string_centered(cx, 178, "% SpO2", TEAL, BLACK, 1);
}
 
static void update_pulse_top_values(const pulse_data_t *pulse) {
    uint16_t cx = LCD_W / 2;
    uint16_t color = pulse->valid ? RED : GREY;
 
    /* Limpia zona BPM */
    fill_rect(30, 55, 180, 70, BLACK);
 
    if (pulse->valid && pulse->bpm > 0) {
        draw_big_number(cx, 58, pulse->bpm, 3, color, BLACK);
        draw_string_centered(cx, 106, "BPM", RED, BLACK, 1);
        /* Corazón animado (doble radio alt) */
        draw_heart_icon(cx, 122, RED);
    } else {
        draw_string_centered(cx, 72, "---", GREY, BLACK, 2);
        draw_string_centered(cx, 106, "BPM", GREY, BLACK, 1);
        /* Indicador de adquisición (arco giratorio simple) */
        draw_arc(cx, 122, 10, 3, 0, 90, GREY);
    }
 
    /* Gráfica en tiempo real */
    draw_bpm_graph(30, 132, 182, 55, RED, BLACK);
}
 
static void update_pulse_bot_values(const pulse_data_t *pulse) {
    uint16_t cx = LCD_W / 2;
 
    /* Limpia zona porcentaje — área ampliada para cubrir posición nueva */
    fill_rect(55, 45, 130, 145, BLACK);
 
    if (pulse->valid && pulse->spo2 > 0) {
        int pct = pulse->spo2;
        if (pct > 100) pct = 100;
        /* Arco de progreso centrado en cy=110 */
        draw_progress_arc(cx, 110, 60, 10, pct, TEAL, BLACK);
        /* Número en el centro del arco */
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        fill_rect(85, 98, 70, 24, BLACK);
        draw_string_centered(cx, 100, buf, WHITE, BLACK, 2);
    } else {
        /* Sin señal */
        draw_progress_arc(cx, 110, 60, 10, 0, GREY, BLACK);
        fill_rect(85, 98, 70, 24, BLACK);
        draw_string_centered(cx, 100, "---", GREY, BLACK, 2);
        draw_string_centered(cx, 130, "MIDIENDO", DKGREY, BLACK, 1);
    }
}
 

/* ═══════════════════════════════════════════════════════════════════════
 *  ANIMACIÓN DE SCROLL VERTICAL
 *  Simula deslizamiento pintando franjas por pasos.
 *  Estrategia: redibujar el contenido de destino de forma secuencial
 *  de arriba-abajo (scroll hacia abajo) o abajo-arriba (scroll up)
 *  usando un offset que crece de 0 a LCD_H en N pasos.
 * ═══════════════════════════════════════════════════════════════════════*/
void display_scroll_to_sub(const qmi8658_data_t *imu,
                           const pulse_data_t   *pulse,
                           display_page_t        page,
                           display_sub_t         target_sub) {
    /*
     * Animación simplificada y rápida:
     * - Dibuja el destino en mitades crecientes de la pantalla.
     * - 12 pasos de ~25 ms cada uno → ~300 ms totales.
     */
    const int steps = 12;
    for (int s = 1; s <= steps; s++) {
        int reveal = (LCD_H * s) / steps;  /* píxeles revelados */

        if (target_sub == SUB_BOT) {
            /* Scroll hacia arriba: el nuevo contenido entra por abajo */
            /* Solo borramos la franja nueva y la pintamos */
            int y0 = LCD_H - reveal;
            fill_rect(0, (uint16_t)y0, LCD_W, (uint16_t)reveal, DKGREY2);
        } else {
            /* Scroll hacia abajo: el nuevo contenido entra por arriba */
            fill_rect(0, 0, LCD_W, (uint16_t)reveal, DKGREY2);
        }
        vTaskDelay(pdMS_TO_TICKS(18));
    }

    /* Dibuja la página destino completa */
    display_show_page(imu, pulse, page, target_sub);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  DASHBOARD PREMIUM
 * ═══════════════════════════════════════════════════════════════════════*/
void display_show_dashboard(void) {
    uint16_t cx = LCD_W / 2;
    uint16_t cy = LCD_H / 2;

    fill_rect(0, 0, LCD_W, LCD_H, BLACK);

    /* Coronas decorativas exteriores */
    draw_arc(cx, cy, 118, 1, 0, 359, RGB565(35,35,45));
    draw_arc(cx, cy, 112, 1, 0, 359, RGB565(50,50,65));

    /* 7 apps periféricas ─────────────────────────────────────────── */
    /* Posiciones calculadas en una corona a r=72 del centro */
    typedef struct { int x; int y; uint16_t bg; void (*icon)(int,int,uint16_t); } AppDef;
    AppDef apps[] = {
        { cx,      cy-72, BLUE,   draw_clock_icon    },
        { cx+62,   cy-38, TEAL,   draw_activity_icon },
        { cx+72,   cy+10, GREEN,  draw_plus_icon     },
        { cx+45,   cy+60, ORANGE, draw_music_icon    },
        { cx-45,   cy+60, PURPLE, draw_message_icon  },
        { cx-72,   cy+10, YELLOW, draw_settings_icon },
        { cx-62,   cy-38, PINK,   draw_heart_icon    },
    };
    for (int i = 0; i < 7; i++) {
        /* Sombra */
        fill_circle(apps[i].x+2, apps[i].y+2, 19, RGB565(15,15,20));
        /* Círculo app */
        fill_circle(apps[i].x, apps[i].y, 18, apps[i].bg);
        /* Borde sutil */
        draw_arc(apps[i].x, apps[i].y, 18, 1, 0, 359, WHITE);
        /* Icono */
        apps[i].icon(apps[i].x, apps[i].y, WHITE);
    }

    /* Botón central NODO1 ──────────────────────────────────────────── */
    /* Sombra */
    fill_circle(cx+3, cy+3, 38, RGB565(80,5,5));
    /* Corona exterior acento */
    fill_circle(cx, cy, 38, DKRED);
    /* Corona interior gradiente simulado */
    fill_circle(cx, cy, 35, ACCENT);
    fill_circle(cx, cy, 32, RED);
    /* Borde brillante superior-izq */
    draw_arc(cx, cy, 35, 2, 210, 330, RGB565(255,120,120));

    /* Corazón */
    draw_heart_icon(cx, cy-8, WHITE);
    draw_string_centered(cx, cy+10, "NODO1", WHITE, RED, 1);

    /* Texto de ayuda */
    draw_string_centered(cx, 210, "TOCA NODO1", GREY, BLACK, 1);

    /* Indicadores IMU y PULSE pequeños en la corona */
    draw_string(10, 108, "IMU", CYAN, BLACK, 1);
    draw_string(200, 108, "HR", RED, BLACK, 1);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  display_show_page  –  Renderiza una página completa
 * ═══════════════════════════════════════════════════════════════════════*/
void display_show_page(const qmi8658_data_t *imu,
                       const pulse_data_t   *pulse,
                       display_page_t        page,
                       display_sub_t         sub) {
    switch (page) {
        case PAGE_IMU:
            if (sub == SUB_TOP) {
                draw_imu_top_static();
                update_imu_top_values(imu);
            } else {
                draw_imu_bot_static();
                update_imu_bot_values(imu);
            }
            break;

        case PAGE_PULSE:
            if (sub == SUB_TOP) {
                draw_pulse_top_static();
                update_pulse_top_values(pulse);
            } else {
                draw_pulse_bot_static();
                update_pulse_bot_values(pulse);
            }
            break;
    }

    draw_scroll_hint(sub);
    display_draw_page_indicator(page, 2);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  display_update_data  –  Refresco parcial (solo zonas dinámicas)
 * ═══════════════════════════════════════════════════════════════════════*/
void display_update_data(const qmi8658_data_t *imu,
                         const pulse_data_t   *pulse,
                         display_page_t        page,
                         display_sub_t         sub) {
    switch (page) {
        case PAGE_IMU:
            if (sub == SUB_TOP) update_imu_top_values(imu);
            else                update_imu_bot_values(imu);
            break;

        case PAGE_PULSE:
            if (sub == SUB_TOP) update_pulse_top_values(pulse);
            else                update_pulse_bot_values(pulse);
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  display_init  –  Sin cambios en SPI/GPIO respecto al original
 * ═══════════════════════════════════════════════════════════════════════*/
esp_err_t display_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<LCD_DC)|(1ULL<<LCD_RST)|(1ULL<<LCD_BL),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(LCD_BL, 1);

    spi_bus_config_t buscfg = {
        .mosi_io_num     = LCD_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_W * 4 * 2,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40*1000*1000,
        .mode           = 0,
        .spics_io_num   = LCD_CS,
        .queue_size     = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi_dev);

    gc9a01_init_seq();
    fill_rect(0, 0, LCD_W, LCD_H, BLACK);
    ESP_LOGI(TAG, "GC9A01 OK – UI premium cargada");
    return ESP_OK;
}