/*
 * ST7701 RGB LCD Driver for MicroPython (Minimal)
 * 
 * Hardware setup only - use framebuf for drawing.
 * Targets ESP32-S3 with esp_lcd RGB panel interface.
 * Display: 480x854 RGB565
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"

#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ST7701";

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
    gpio_num_t data[16];

    // Cached framebuffer memoryview
    mp_obj_t fb_obj;
} st7701_obj_t;

const mp_obj_type_t st7701_type;

// ============================================================================
// 9-bit SPI bit-bang for init sequence
// ============================================================================

static void spi_write_9bit(st7701_obj_t *self, bool is_data, uint8_t val) {
    gpio_set_level(self->spi_cs, 0);
    esp_rom_delay_us(1);
    
    gpio_set_level(self->spi_clk, 0);
    esp_rom_delay_us(1);
    gpio_set_level(self->spi_mosi, is_data ? 1 : 0);
    esp_rom_delay_us(1);
    gpio_set_level(self->spi_clk, 1);
    esp_rom_delay_us(1);
    
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
        .pin_bit_mask = (1ULL << self->spi_cs) | 
                        (1ULL << self->spi_clk) | 
                        (1ULL << self->spi_mosi) |
                        (1ULL << self->reset),
    };
    gpio_config(&io_conf);
    
    gpio_set_level(self->spi_cs, 1);
    gpio_set_level(self->spi_clk, 1);
    gpio_set_level(self->reset, 1);
}

static void setup_backlight(st7701_obj_t *self, bool on) {
    if (self->backlight >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << self->backlight),
        };
        gpio_config(&io_conf);
        gpio_set_level(self->backlight, on ? 1 : 0);
    }
}

// ============================================================================
// RGB Panel Setup
// ============================================================================

static esp_err_t setup_rgb_panel(st7701_obj_t *self) {
    ESP_LOGI(TAG, "Setting up RGB panel %dx%d", self->width, self->height);
    
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = 16000000,
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
            },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = self->width * 7,  // 480*7 divides into 480*854
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
        },
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &self->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(self->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(self->panel_handle));
    
    esp_lcd_rgb_panel_get_frame_buffer(self->panel_handle, 1, (void **)&self->framebuffer);
    
    ESP_LOGI(TAG, "RGB panel ready, framebuffer at %p", self->framebuffer);
    
    return ESP_OK;
}

// ============================================================================
// MicroPython Interface
// ============================================================================

// Constructor
static mp_obj_t st7701_make_new(const mp_obj_type_t *type, size_t n_args, 
                                  size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 10, 10, false);
    
    st7701_obj_t *self = m_new_obj(st7701_obj_t);
    self->base.type = &st7701_type;
    self->width = LCD_H_RES;
    self->height = LCD_V_RES;
    self->panel_handle = NULL;
    self->framebuffer = NULL;
    self->fb_obj = mp_const_none; 
    
    self->spi_cs = mp_obj_get_int(args[0]);
    self->spi_clk = mp_obj_get_int(args[1]);
    self->spi_mosi = mp_obj_get_int(args[2]);
    self->reset = mp_obj_get_int(args[3]);
    self->backlight = mp_obj_get_int(args[4]);
    
    self->pclk = mp_obj_get_int(args[5]);
    self->hsync = mp_obj_get_int(args[6]);
    self->vsync = mp_obj_get_int(args[7]);
    self->de = mp_obj_get_int(args[8]);
    
    mp_obj_t *data_pins;
    size_t data_pins_len;
    mp_obj_get_array(args[9], &data_pins_len, &data_pins);
    
    if (data_pins_len != 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("data_pins must have 16 elements"));
    }
    
    for (int i = 0; i < 16; i++) {
        self->data[i] = mp_obj_get_int(data_pins[i]);
    }
    
    return MP_OBJ_FROM_PTR(self);
}

// init()
static mp_obj_t st7701_init(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    setup_spi_gpio(self);
    st7701_init_sequence(self);
    setup_rgb_panel(self);
    setup_backlight(self, true);
    
    // Clear to black
    memset(self->framebuffer, 0, self->width * self->height * 2);
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_init_obj, st7701_init);

static mp_obj_t st7701_deinit(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Turn off backlight first
    if (self->backlight >= 0) {
        gpio_set_level(self->backlight, 0);
    }
    
    if (self->panel_handle != NULL) {
        esp_lcd_panel_del(self->panel_handle);
        self->panel_handle = NULL;
        self->framebuffer = NULL;
        self->fb_obj = mp_const_none;  // invalidate cached memoryview
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_deinit_obj, st7701_deinit);

// framebuffer() -> memoryview
static mp_obj_t st7701_framebuffer(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Not initialized"));
    }
    
    if (self->fb_obj == mp_const_none) {
        size_t size = self->width * self->height * 2;
        //return mp_obj_new_bytearray_by_ref(size, self->framebuffer);
        self->fb_obj = mp_obj_new_memoryview('B' | 0x80, size, self->framebuffer);
    }
    
    return self->fb_obj;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_framebuffer_obj, st7701_framebuffer);

// width()
static mp_obj_t st7701_width(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->width);
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_width_obj, st7701_width);

// height()
static mp_obj_t st7701_height(mp_obj_t self_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->height);
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_height_obj, st7701_height);

// backlight(on/off)
static mp_obj_t st7701_backlight(mp_obj_t self_in, mp_obj_t on_in) {
    st7701_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    if (self->backlight >= 0) {
        gpio_set_level(self->backlight, mp_obj_is_true(on_in) ? 1 : 0);
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7701_backlight_obj, st7701_backlight);

// Swap bytes in-place for big-endian RGB565 data
static mp_obj_t st7701_swap_bytes(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_RW);
    
    uint16_t *buf = bufinfo.buf;
    size_t len = bufinfo.len / 2;
    
    for (size_t i = 0; i < len; i++) {
        buf[i] = __builtin_bswap16(buf[i]);
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7701_swap_bytes_obj, st7701_swap_bytes);

// rgb565(r, g, b) - Convert RGB888 to RGB565
static mp_obj_t st7701_rgb565(size_t n_args, const mp_obj_t *args) {
    int r = mp_obj_get_int(args[0]) & 0xFF;
    int g = mp_obj_get_int(args[1]) & 0xFF;
    int b = mp_obj_get_int(args[2]) & 0xFF;
    
    uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return mp_obj_new_int(color);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_rgb565_obj, 3, 3, st7701_rgb565);

// Helper: Vertical flip
static void flip_vertical(uint16_t *buffer, int w, int h) {
    for (int y = 0; y < h / 2; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t temp = buffer[y * w + x];
            buffer[y * w + x] = buffer[(h - 1 - y) * w + x];
            buffer[(h - 1 - y) * w + x] = temp;
        }
    }
}

// Helper: In-place rectangular transpose using cycle-following
static void transpose_rectangular(uint16_t *buffer, int w, int h) {
    int n = w * h;
    int mod = n - 1;
    
    for (int start = 1; start < n - 1; start++) {
        int next = (start * h) % mod;
        
        // Only process if start is minimum index in cycle
        int current = next;
        int is_min = 1;
        while (current != start) {
            if (current < start) {
                is_min = 0;
                break;
            }
            current = (current * h) % mod;
        }
        
        if (is_min) {
            uint16_t val = buffer[start];
            int curr_idx = start;
            do {
                int next_idx = (curr_idx * h) % mod;
                uint16_t temp = buffer[next_idx];
                buffer[next_idx] = val;
                val = temp;
                curr_idx = next_idx;
            } while (curr_idx != start);
        }
    }
}

// Rotate in-place by 90, 180, or 270 degrees - avoids any new memory allocation at the expense of more complexity
// rotate(buffer, width, height, degrees) -> (buffer, new_width, new_height)
static mp_obj_t st7701_rotate(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_RW);
    
    mp_int_t w = mp_obj_get_int(args[1]);
    mp_int_t h = mp_obj_get_int(args[2]);
    mp_int_t degrees = mp_obj_get_int(args[3]);
    
    size_t expected_size = w * h * 2;
    if (bufinfo.len < expected_size) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small for dimensions"));
    }
    
    if (degrees != 90 && degrees != 180 && degrees != 270) {
        mp_raise_ValueError(MP_ERROR_TEXT("degrees must be 90, 180, or 270"));
    }
    
    uint16_t *buffer = bufinfo.buf;
    int n = w * h;
    
    mp_int_t new_w, new_h;
    
    if (degrees == 180) {
        // Reverse entire array
        for (int i = 0; i < n / 2; i++) {
            uint16_t temp = buffer[i];
            buffer[i] = buffer[n - 1 - i];
            buffer[n - 1 - i] = temp;
        }
        new_w = w;
        new_h = h;
    } 
    else if (degrees == 90) {
        // 90 CW = Vertical flip -> Transpose
        flip_vertical(buffer, w, h);
        transpose_rectangular(buffer, w, h);
        new_w = h;
        new_h = w;
    } 
    else {  // 270
        // 270 CW = Transpose -> Vertical flip (with swapped dims)
        transpose_rectangular(buffer, w, h);
        flip_vertical(buffer, h, w);
        new_w = h;
        new_h = w;
    }
    
    mp_obj_t tuple[2] = {
        mp_obj_new_int(new_w),
        mp_obj_new_int(new_h)
    };
    
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7701_rotate_obj, 4, 4, st7701_rotate);

// ============================================================================
// Module Registration
// ============================================================================

static const mp_rom_map_elem_t st7701_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),        MP_ROM_PTR(&st7701_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),        MP_ROM_PTR(&st7701_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_framebuffer), MP_ROM_PTR(&st7701_framebuffer_obj) },
    { MP_ROM_QSTR(MP_QSTR_width),       MP_ROM_PTR(&st7701_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height),      MP_ROM_PTR(&st7701_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_backlight),   MP_ROM_PTR(&st7701_backlight_obj) },
};
static MP_DEFINE_CONST_DICT(st7701_locals_dict, st7701_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    st7701_type,
    MP_QSTR_ST7701,
    MP_TYPE_FLAG_NONE,
    make_new, st7701_make_new,
    locals_dict, &st7701_locals_dict
);

static const mp_rom_map_elem_t st7701_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_st7701) },
    { MP_ROM_QSTR(MP_QSTR_ST7701),      MP_ROM_PTR(&st7701_type) },
    { MP_ROM_QSTR(MP_QSTR_swap_bytes),  MP_ROM_PTR(&st7701_swap_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_rgb565),      MP_ROM_PTR(&st7701_rgb565_obj) },
    { MP_ROM_QSTR(MP_QSTR_rotate),      MP_ROM_PTR(&st7701_rotate_obj) },

    // Module-level color constants
    { MP_ROM_QSTR(MP_QSTR_BLACK),       MP_ROM_INT(COLOR_BLACK) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),       MP_ROM_INT(COLOR_WHITE) },
    { MP_ROM_QSTR(MP_QSTR_RED),         MP_ROM_INT(COLOR_RED) },
    { MP_ROM_QSTR(MP_QSTR_GREEN),       MP_ROM_INT(COLOR_GREEN) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),        MP_ROM_INT(COLOR_BLUE) },
};
static MP_DEFINE_CONST_DICT(st7701_module_globals, st7701_module_globals_table);

const mp_obj_module_t st7701_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&st7701_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_st7701, st7701_module);