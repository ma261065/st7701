/*
 * ST7701 RGB LCD Driver for MicroPython
 * 
 * Targets ESP32-S3 with esp_lcd RGB panel interface
 * Display: 480x854 18-bit RGB (wired as 16-bit RGB565)
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"

#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ST7701";

// Display dimensions
#define LCD_H_RES 480
#define LCD_V_RES 854

// Color definitions (RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F

// ============================================================================
// ST7701 Display Object
// ============================================================================

typedef struct _st7701_obj_t {
    mp_obj_base_t base;
    esp_lcd_panel_handle_t panel_handle;
    uint16_t *framebuffer;
    uint16_t width;
    uint16_t height;
    
    // SPI pins for init
    gpio_num_t spi_cs;
    gpio_num_t spi_clk;
    gpio_num_t spi_mosi;
    gpio_num_t reset;
    gpio_num_t backlight;
    
    // RGB pins
    gpio_num_t pclk;
    gpio_num_t hsync;
    gpio_num_t vsync;
    gpio_num_t de;
    gpio_num_t data[16];  // RGB565: B0-B4, G0-G5, R0-R4
} st7701_obj_t;

const mp_obj_type_t st7701_type;

// ============================================================================
// 9-bit SPI bit-bang for init sequence
// ============================================================================

static void spi_write_9bit(st7701_obj_t *self, bool is_data, uint8_t val) {
    gpio_set_level(self->spi_cs, 0);
    esp_rom_delay_us(1);
    
    // First bit: 0 = command, 1 = data
    gpio_set_level(self->spi_clk, 0);
    esp_rom_delay_us(1);
    gpio_set_level(self->spi_mosi, is_data ? 1 : 0);
    esp_rom_delay_us(1);
    gpio_set_level(self->spi_clk, 1);
    esp_rom_delay_us(1);
    
    // Remaining 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(self->spi_clk, 0);
        esp_rom_delay_us(1);
        gpio_set_level(self->spi_mosi, (val >> i) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(self->spi_clk, 1);
        esp_rom_delay_us(1);
    }
    
    gpio_set_level(self->spi_cs, 1);
    esp_rom_delay_us(1);
}

static inline void lcd_cmd(st7701_obj_t *self, uint8_t cmd) {
    spi_write_9bit(self, false, cmd);
}

static inline void lcd_data(st7701_obj_t *self, uint8_t data) {
    spi_write_9bit(self, true, data);
}

// ============================================================================
// ST7701 Initialization Sequence (from vendor)
// ============================================================================

static void st7701_init_sequence(st7701_obj_t *self) {
    // Hardware reset
    gpio_set_level(self->reset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(self->reset, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(self->reset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Page 13 (vendor-specific)
    lcd_cmd(self, 0xFF);
    lcd_data(self, 0x77); lcd_data(self, 0x01); lcd_data(self, 0x00);
    lcd_data(self, 0x00); lcd_data(self, 0x13);
    lcd_cmd(self, 0xEF);
    lcd_data(self, 0x08);
    
    // Page 10 (display settings)
    lcd_cmd(self, 0xFF);
    lcd_data(self, 0x77); lcd_data(self, 0x01); lcd_data(self, 0x00);
    lcd_data(self, 0x00); lcd_data(self, 0x10);
    
    lcd_cmd(self, 0xC0);
    lcd_data(self, 0xE9); lcd_data(self, 0x03);
    
    lcd_cmd(self, 0xC1);
    lcd_data(self, 0x10); lcd_data(self, 0x0C);
    
    lcd_cmd(self, 0xC2);
    lcd_data(self, 0x20); lcd_data(self, 0x0A);
    
    lcd_cmd(self, 0xCC);
    lcd_data(self, 0x10);
    
    // Positive gamma
    lcd_cmd(self, 0xB0);
    lcd_data(self, 0x07); lcd_data(self, 0x14); lcd_data(self, 0x9C); lcd_data(self, 0x0B);
    lcd_data(self, 0x10); lcd_data(self, 0x06); lcd_data(self, 0x08); lcd_data(self, 0x09);
    lcd_data(self, 0x08); lcd_data(self, 0x22); lcd_data(self, 0x02); lcd_data(self, 0x4F);
    lcd_data(self, 0x0E); lcd_data(self, 0x66); lcd_data(self, 0x2D); lcd_data(self, 0x1C);
    
    // Negative gamma
    lcd_cmd(self, 0xB1);
    lcd_data(self, 0x09); lcd_data(self, 0x17); lcd_data(self, 0x9E); lcd_data(self, 0x0F);
    lcd_data(self, 0x11); lcd_data(self, 0x06); lcd_data(self, 0x0C); lcd_data(self, 0x08);
    lcd_data(self, 0x08); lcd_data(self, 0x26); lcd_data(self, 0x04); lcd_data(self, 0x51);
    lcd_data(self, 0x10); lcd_data(self, 0x6A); lcd_data(self, 0x33); lcd_data(self, 0x1D);
    
    // Page 11 (power settings)
    lcd_cmd(self, 0xFF);
    lcd_data(self, 0x77); lcd_data(self, 0x01); lcd_data(self, 0x00);
    lcd_data(self, 0x00); lcd_data(self, 0x11);
    
    lcd_cmd(self, 0xB0); lcd_data(self, 0x4D);
    lcd_cmd(self, 0xB1); lcd_data(self, 0x43);
    lcd_cmd(self, 0xB2); lcd_data(self, 0x84);
    lcd_cmd(self, 0xB3); lcd_data(self, 0x80);
    lcd_cmd(self, 0xB5); lcd_data(self, 0x45);
    lcd_cmd(self, 0xB7); lcd_data(self, 0x85);
    lcd_cmd(self, 0xB8); lcd_data(self, 0x33);
    lcd_cmd(self, 0xC1); lcd_data(self, 0x78);
    lcd_cmd(self, 0xC2); lcd_data(self, 0x78);
    lcd_cmd(self, 0xD0); lcd_data(self, 0x88);
    
    // GIP timing (Gate-in-Panel)
    lcd_cmd(self, 0xE0);
    lcd_data(self, 0x00); lcd_data(self, 0x00); lcd_data(self, 0x02);
    
    lcd_cmd(self, 0xE1);
    lcd_data(self, 0x06); lcd_data(self, 0xA0); lcd_data(self, 0x08); lcd_data(self, 0xA0);
    lcd_data(self, 0x05); lcd_data(self, 0xA0); lcd_data(self, 0x07); lcd_data(self, 0xA0);
    lcd_data(self, 0x00); lcd_data(self, 0x44); lcd_data(self, 0x44);
    
    lcd_cmd(self, 0xE2);
    lcd_data(self, 0x30); lcd_data(self, 0x30); lcd_data(self, 0x44); lcd_data(self, 0x44);
    lcd_data(self, 0x6E); lcd_data(self, 0xA0); lcd_data(self, 0x00); lcd_data(self, 0x00);
    lcd_data(self, 0x6E); lcd_data(self, 0xA0); lcd_data(self, 0x00); lcd_data(self, 0x00);
    
    lcd_cmd(self, 0xE3);
    lcd_data(self, 0x00); lcd_data(self, 0x00); lcd_data(self, 0x33); lcd_data(self, 0x33);
    
    lcd_cmd(self, 0xE4);
    lcd_data(self, 0x44); lcd_data(self, 0x44);
    
    lcd_cmd(self, 0xE5);
    lcd_data(self, 0x0D); lcd_data(self, 0x69); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    lcd_data(self, 0x0F); lcd_data(self, 0x6B); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    lcd_data(self, 0x09); lcd_data(self, 0x65); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    lcd_data(self, 0x0B); lcd_data(self, 0x67); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    
    lcd_cmd(self, 0xE6);
    lcd_data(self, 0x00); lcd_data(self, 0x00); lcd_data(self, 0x33); lcd_data(self, 0x33);
    
    lcd_cmd(self, 0xE7);
    lcd_data(self, 0x44); lcd_data(self, 0x44);
    
    lcd_cmd(self, 0xE8);
    lcd_data(self, 0x0C); lcd_data(self, 0x68); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    lcd_data(self, 0x0E); lcd_data(self, 0x6A); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    lcd_data(self, 0x08); lcd_data(self, 0x64); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    lcd_data(self, 0x0A); lcd_data(self, 0x66); lcd_data(self, 0x0A); lcd_data(self, 0xA0);
    
    lcd_cmd(self, 0xE9);
    lcd_data(self, 0x36); lcd_data(self, 0x00);
    
    lcd_cmd(self, 0xEB);
    lcd_data(self, 0x00); lcd_data(self, 0x01); lcd_data(self, 0xE4); lcd_data(self, 0xE4);
    lcd_data(self, 0x44); lcd_data(self, 0x88); lcd_data(self, 0x40);
    
    lcd_cmd(self, 0xED);
    lcd_data(self, 0xFF); lcd_data(self, 0x45); lcd_data(self, 0x67); lcd_data(self, 0xFA);
    lcd_data(self, 0x01); lcd_data(self, 0x2B); lcd_data(self, 0xCF); lcd_data(self, 0xFF);
    lcd_data(self, 0xFF); lcd_data(self, 0xFC); lcd_data(self, 0xB2); lcd_data(self, 0x10);
    lcd_data(self, 0xAF); lcd_data(self, 0x76); lcd_data(self, 0x54); lcd_data(self, 0xFF);
    
    lcd_cmd(self, 0xEF);
    lcd_data(self, 0x10); lcd_data(self, 0x0D); lcd_data(self, 0x04); lcd_data(self, 0x08);
    lcd_data(self, 0x3F); lcd_data(self, 0x1F);
    
    // Pixel format: RGB565 (16-bit)
    // Use 0x55 for RGB565, 0x60 for RGB666
    lcd_cmd(self, 0x3A);
    lcd_data(self, 0x55);
    
    // Sleep out
    lcd_cmd(self, 0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Tearing effect on
    lcd_cmd(self, 0x35);
    lcd_data(self, 0x00);
    
    // Display on
    lcd_cmd(self, 0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    ESP_LOGI(TAG, "ST7701 init sequence complete");
}

// ============================================================================
// GPIO Setup
// ============================================================================

static void setup_spi_gpio(st7701_obj_t *self) {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    io_conf.pin_bit_mask = (1ULL << self->spi_cs) | 
                           (1ULL << self->spi_clk) | 
                           (1ULL << self->spi_mosi) |
                           (1ULL << self->reset);
    gpio_config(&io_conf);
    
    // Default states
    gpio_set_level(self->spi_cs, 1);
    gpio_set_level(self->spi_clk, 1);
    gpio_set_level(self->reset, 1);
}

static void setup_backlight(st7701_obj_t *self) {
    if (self->backlight >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << self->backlight),
        };
        gpio_config(&io_conf);
        gpio_set_level(self->backlight, 1);  // On by default
    }
}

// ============================================================================
// RGB Panel Setup via esp_lcd
// ============================================================================

static esp_err_t setup_rgb_panel(st7701_obj_t *self) {
    ESP_LOGI(TAG, "Setting up RGB panel %dx%d", self->width, self->height);
    
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = 12000000,  // 12MHz - adjust if needed
            .h_res = self->width,
            .v_res = self->height,
            .hsync_pulse_width = 10,
            .hsync_back_porch = 50,
            .hsync_front_porch = 10,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 20,
            .vsync_front_porch = 10,
            .flags = {
                .pclk_active_neg = 0,
                .hsync_idle_low = 0,
                .vsync_idle_low = 0,
                .de_idle_high = 0,
            },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = 480 * 7, //was self->width * 10,
        .sram_trans_align = 8,
        .psram_trans_align = 64,
        .hsync_gpio_num = self->hsync,
        .vsync_gpio_num = self->vsync,
        .de_gpio_num = self->de,
        .pclk_gpio_num = self->pclk,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            self->data[0],  self->data[1],  self->data[2],  self->data[3],
            self->data[4],  self->data[5],  self->data[6],  self->data[7],
            self->data[8],  self->data[9],  self->data[10], self->data[11],
            self->data[12], self->data[13], self->data[14], self->data[15],
        },
        .flags = {
            .fb_in_psram = 1,
            .double_fb = 0,
            .no_fb = 0,
            .refresh_on_demand = 0,
        },
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &self->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(self->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(self->panel_handle));
    
    // Get framebuffer pointer
    esp_lcd_rgb_panel_get_frame_buffer(self->panel_handle, 1, (void **)&self->framebuffer);
    
    ESP_LOGI(TAG, "RGB panel initialized, framebuffer at %p", self->framebuffer);
    
    return ESP_OK;
}

// ============================================================================
// MicroPython Interface
// ============================================================================

// Constructor: ST7701(spi_cs, spi_clk, spi_mosi, reset, backlight,
//                      pclk, hsync, vsync, de, data_pins)
static mp_obj_t st7701_make_new(const mp_obj_type_t *type, size_t n_args, 
                                  size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 10, 10, false);
    
    st7701_obj_t *self = m_new_obj(st7701_obj_t);
    self->base.type = &st7701_type;
    self->width = LCD_H_RES;
    self->height = LCD_V_RES;
    self->panel_handle = NULL;
    self->framebuffer = NULL;
    
    // SPI init pins
    self->spi_cs = mp_obj_get_int(args[0]);
    self->spi_clk = mp_obj_get_int(args[1]);
    self->spi_mosi = mp_obj_get_int(args[2]);
    self->reset = mp_obj_get_int(args[3]);
    self->backlight = mp_obj_get_int(args[4]);
    
    // RGB control pins
    self->pclk = mp_obj_get_int(args[5]);
    self->hsync = mp_obj_get_int(args[6]);
    self->vsync = mp_obj_get_int(args[7]);
    self->de = mp_obj_get_int(args[8]);
    
    // RGB data pins (list of 16 GPIO numbers)
    mp_obj_t *data_pins;
    size_t data_pins_len;
    mp_obj_get_array(args[9], &data_pins_len, &data_pins);
    
    if (data_pins_len != 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("data_pins must be list of 16 GPIO numbers"));
    }
    
    for (int i = 0; i < 16; i++) {
        self->data[i] = mp_obj_get_int(data_pins[i]);
    }
    
    return MP_OBJ_FROM_PTR(self);
}

// init() - Initialize the display
static mp_obj_t st7701_init(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    ESP_LOGI(TAG, "Initializing ST7701 %dx%d", self->width, self->height);
    
    // Setup GPIO for SPI init
    setup_spi_gpio(self);
    
    // Run ST7701 init sequence via 9-bit SPI
    st7701_init_sequence(self);
    
    // Setup RGB panel
    setup_rgb_panel(self);
    
    // Setup backlight
    setup_backlight(self);
    
    // Clear screen to black
    memset(self->framebuffer, 0, self->width * self->height * 2);
    
    ESP_LOGI(TAG, "ST7701 ready");
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_init_obj, st7701_init);

// fill(color) - Fill entire screen with RGB565 color
static mp_obj_t st7701_fill(mp_obj_t self_in, mp_obj_t color_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint16_t color = mp_obj_get_int(color_in);
    
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Display not initialized"));
    }
    
    size_t pixels = self->width * self->height;
    for (size_t i = 0; i < pixels; i++) {
        self->framebuffer[i] = color;
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7701_fill_obj, st7701_fill);

// pixel(x, y, color) - Set single pixel
static mp_obj_t st7701_pixel(size_t n_args, const mp_obj_t *args) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    uint16_t color = mp_obj_get_int(args[3]);
    
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Display not initialized"));
    }
    
    if (x >= 0 && x < self->width && y >= 0 && y < self->height) {
        self->framebuffer[y * self->width + x] = color;
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_pixel_obj, 4, 4, st7701_pixel);

// fill_rect(x, y, w, h, color) - Fill rectangle
static mp_obj_t st7701_fill_rect(size_t n_args, const mp_obj_t *args) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int w = mp_obj_get_int(args[3]);
    int h = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);
    
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Display not initialized"));
    }
    
    // Clip to screen bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > self->width) { w = self->width - x; }
    if (y + h > self->height) { h = self->height - y; }
    
    if (w <= 0 || h <= 0) return mp_const_none;
    
    for (int row = y; row < y + h; row++) {
        uint16_t *line = &self->framebuffer[row * self->width + x];
        for (int col = 0; col < w; col++) {
            line[col] = color;
        }
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_fill_rect_obj, 6, 6, st7701_fill_rect);

// hline(x, y, w, color) - Horizontal line
static mp_obj_t st7701_hline(size_t n_args, const mp_obj_t *args) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int w = mp_obj_get_int(args[3]);
    uint16_t color = mp_obj_get_int(args[4]);
    
    if (self->framebuffer == NULL || y < 0 || y >= self->height) {
        return mp_const_none;
    }
    
    if (x < 0) { w += x; x = 0; }
    if (x + w > self->width) { w = self->width - x; }
    if (w <= 0) return mp_const_none;
    
    uint16_t *line = &self->framebuffer[y * self->width + x];
    for (int i = 0; i < w; i++) {
        line[i] = color;
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_hline_obj, 5, 5, st7701_hline);

// vline(x, y, h, color) - Vertical line
static mp_obj_t st7701_vline(size_t n_args, const mp_obj_t *args) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int h = mp_obj_get_int(args[3]);
    uint16_t color = mp_obj_get_int(args[4]);
    
    if (self->framebuffer == NULL || x < 0 || x >= self->width) {
        return mp_const_none;
    }
    
    if (y < 0) { h += y; y = 0; }
    if (y + h > self->height) { h = self->height - y; }
    if (h <= 0) return mp_const_none;
    
    for (int row = y; row < y + h; row++) {
        self->framebuffer[row * self->width + x] = color;
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_vline_obj, 5, 5, st7701_vline);

// blit(buffer, x, y, w, h) - Blit a buffer (bytes/bytearray) to screen
static mp_obj_t st7701_blit(size_t n_args, const mp_obj_t *args) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_READ);
    
    int x = mp_obj_get_int(args[2]);
    int y = mp_obj_get_int(args[3]);
    int w = mp_obj_get_int(args[4]);
    int h = mp_obj_get_int(args[5]);
    
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Display not initialized"));
    }
    
    // Check buffer size
    size_t expected_size = w * h * 2;
    if (bufinfo.len < expected_size) {
        mp_raise_ValueError(MP_ERROR_TEXT("Buffer too small"));
    }
    
    uint16_t *src = (uint16_t *)bufinfo.buf;
    
    // Simple blit with clipping
    int src_x = 0, src_y = 0;
    int dst_x = x, dst_y = y;
    int copy_w = w, copy_h = h;
    
    // Clip left
    if (dst_x < 0) {
        src_x = -dst_x;
        copy_w += dst_x;
        dst_x = 0;
    }
    // Clip top
    if (dst_y < 0) {
        src_y = -dst_y;
        copy_h += dst_y;
        dst_y = 0;
    }
    // Clip right
    if (dst_x + copy_w > self->width) {
        copy_w = self->width - dst_x;
    }
    // Clip bottom
    if (dst_y + copy_h > self->height) {
        copy_h = self->height - dst_y;
    }
    
    if (copy_w <= 0 || copy_h <= 0) return mp_const_none;
    
    for (int row = 0; row < copy_h; row++) {
        uint16_t *dst_line = &self->framebuffer[(dst_y + row) * self->width + dst_x];
        uint16_t *src_line = &src[(src_y + row) * w + src_x];
        memcpy(dst_line, src_line, copy_w * 2);
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_blit_obj, 6, 6, st7701_blit);

// backlight(on) - Control backlight (0=off, 1=on)
static mp_obj_t st7701_backlight(mp_obj_t self_in, mp_obj_t on_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    if (self->backlight >= 0) {
        gpio_set_level(self->backlight, mp_obj_is_true(on_in) ? 1 : 0);
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7701_backlight_obj, st7701_backlight);

// width() - Get display width
static mp_obj_t st7701_width(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->width);
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_width_obj, st7701_width);

// height() - Get display height
static mp_obj_t st7701_height(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->height);
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_height_obj, st7701_height);

// framebuffer() - Get direct access to framebuffer as memoryview
static mp_obj_t st7701_framebuffer_obj(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Display not initialized"));
    }
    
    size_t size = self->width * self->height * 2;
    return mp_obj_new_memoryview('B', size, self->framebuffer);
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_framebuffer_method, st7701_framebuffer_obj);

// color565(r, g, b) - Convert RGB888 to RGB565
static mp_obj_t st7701_color565(size_t n_args, const mp_obj_t *args) {
    (void)args[0];  // self, unused
    int r = mp_obj_get_int(args[1]) & 0xFF;
    int g = mp_obj_get_int(args[2]) & 0xFF;
    int b = mp_obj_get_int(args[3]) & 0xFF;
    
    uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return mp_obj_new_int(color);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_color565_obj, 4, 4, st7701_color565);

// ============================================================================
// Module Registration
// ============================================================================

static const mp_rom_map_elem_t st7701_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),        MP_ROM_PTR(&st7701_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill),        MP_ROM_PTR(&st7701_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel),       MP_ROM_PTR(&st7701_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect),   MP_ROM_PTR(&st7701_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_hline),       MP_ROM_PTR(&st7701_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_vline),       MP_ROM_PTR(&st7701_vline_obj) },
    { MP_ROM_QSTR(MP_QSTR_blit),        MP_ROM_PTR(&st7701_blit_obj) },
    { MP_ROM_QSTR(MP_QSTR_backlight),   MP_ROM_PTR(&st7701_backlight_obj) },
    { MP_ROM_QSTR(MP_QSTR_width),       MP_ROM_PTR(&st7701_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height),      MP_ROM_PTR(&st7701_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_framebuffer), MP_ROM_PTR(&st7701_framebuffer_method) },
    { MP_ROM_QSTR(MP_QSTR_color565),    MP_ROM_PTR(&st7701_color565_obj) },
    
    // Color constants
    { MP_ROM_QSTR(MP_QSTR_BLACK),       MP_ROM_INT(COLOR_BLACK) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),       MP_ROM_INT(COLOR_WHITE) },
    { MP_ROM_QSTR(MP_QSTR_RED),         MP_ROM_INT(COLOR_RED) },
    { MP_ROM_QSTR(MP_QSTR_GREEN),       MP_ROM_INT(COLOR_GREEN) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),        MP_ROM_INT(COLOR_BLUE) },
};
static MP_DEFINE_CONST_DICT(st7701_locals_dict, st7701_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    st7701_type,
    MP_QSTR_ST7701,
    MP_TYPE_FLAG_NONE,
    make_new, st7701_make_new,
    locals_dict, &st7701_locals_dict
);

// Module-level definitions
static const mp_rom_map_elem_t st7701_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_st7701) },
    { MP_ROM_QSTR(MP_QSTR_ST7701),  MP_ROM_PTR(&st7701_type) },
    
    // Module-level color constants
    { MP_ROM_QSTR(MP_QSTR_BLACK),    MP_ROM_INT(COLOR_BLACK) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),    MP_ROM_INT(COLOR_WHITE) },
    { MP_ROM_QSTR(MP_QSTR_RED),      MP_ROM_INT(COLOR_RED) },
    { MP_ROM_QSTR(MP_QSTR_GREEN),    MP_ROM_INT(COLOR_GREEN) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),     MP_ROM_INT(COLOR_BLUE) },
};
static MP_DEFINE_CONST_DICT(st7701_module_globals, st7701_module_globals_table);

const mp_obj_module_t st7701_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&st7701_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_st7701, st7701_module);
