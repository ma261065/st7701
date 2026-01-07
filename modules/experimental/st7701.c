/*
 * ST7701S RGB LCD Driver for MicroPython (Minimal)
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

static const char *TAG = "ST7701S";

#define LCD_H_RES 480
#define LCD_V_RES 854

// ============================================================================
// ST7701S Display Object
// ============================================================================

typedef struct _st7701s_obj_t {
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
} st7701s_obj_t;

const mp_obj_type_t st7701s_type;

// ============================================================================
// 9-bit SPI bit-bang for init sequence
// ============================================================================

static void spi_write_9bit(st7701s_obj_t *self, bool is_data, uint8_t val) {
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

static inline void lcd_cmd(st7701s_obj_t *self, uint8_t cmd) {
    spi_write_9bit(self, false, cmd);
}

static inline void lcd_data(st7701s_obj_t *self, uint8_t data) {
    spi_write_9bit(self, true, data);
}

// ============================================================================
// ST7701S Initialization Sequence
// ============================================================================

static void st7701s_init_sequence(st7701s_obj_t *self) {
    gpio_set_level(self->reset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(self->reset, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(self->reset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Page 13
    lcd_cmd(self, 0xFF);
    lcd_data(self, 0x77); lcd_data(self, 0x01); lcd_data(self, 0x00);
    lcd_data(self, 0x00); lcd_data(self, 0x13);
    lcd_cmd(self, 0xEF); lcd_data(self, 0x08);
    
    // Page 10
    lcd_cmd(self, 0xFF);
    lcd_data(self, 0x77); lcd_data(self, 0x01); lcd_data(self, 0x00);
    lcd_data(self, 0x00); lcd_data(self, 0x10);
    
    lcd_cmd(self, 0xC0); lcd_data(self, 0xE9); lcd_data(self, 0x03);
    lcd_cmd(self, 0xC1); lcd_data(self, 0x10); lcd_data(self, 0x0C);
    lcd_cmd(self, 0xC2); lcd_data(self, 0x20); lcd_data(self, 0x0A);
    lcd_cmd(self, 0xCC); lcd_data(self, 0x10);
    
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
    
    // Page 11
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
    
    // GIP timing
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
    
    // RGB565
    lcd_cmd(self, 0x3A); lcd_data(self, 0x55);
    
    // Sleep out
    lcd_cmd(self, 0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // TE on
    lcd_cmd(self, 0x35); lcd_data(self, 0x00);
    
    // Display on
    lcd_cmd(self, 0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    ESP_LOGI(TAG, "ST7701S init complete");
}

// ============================================================================
// GPIO Setup
// ============================================================================

static void setup_spi_gpio(st7701s_obj_t *self) {
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

static void setup_backlight(st7701s_obj_t *self, bool on) {
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

static esp_err_t setup_rgb_panel(st7701s_obj_t *self) {
    ESP_LOGI(TAG, "Setting up RGB panel %dx%d", self->width, self->height);
    
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = 12000000,
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
STATIC mp_obj_t st7701s_make_new(const mp_obj_type_t *type, size_t n_args, 
                                  size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 10, 10, false);
    
    st7701s_obj_t *self = m_new_obj(st7701s_obj_t);
    self->base.type = &st7701s_type;
    self->width = LCD_H_RES;
    self->height = LCD_V_RES;
    self->panel_handle = NULL;
    self->framebuffer = NULL;
    
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
STATIC mp_obj_t st7701s_init(mp_obj_t self_in) {
    st7701s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    setup_spi_gpio(self);
    st7701s_init_sequence(self);
    setup_rgb_panel(self);
    setup_backlight(self, true);
    
    // Clear to black
    memset(self->framebuffer, 0, self->width * self->height * 2);
    
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(st7701s_init_obj, st7701s_init);

// framebuffer() -> bytearray
STATIC mp_obj_t st7701s_framebuffer(mp_obj_t self_in) {
    st7701s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Not initialized"));
    }
    
    size_t size = self->width * self->height * 2;
    //return mp_obj_new_bytearray_by_ref(size, self->framebuffer);
    // Writable memoryview - set bit 7 of typecode
    return mp_obj_new_memoryview('B' | 0x80, size, self->framebuffer);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(st7701s_framebuffer_obj, st7701s_framebuffer);

// width()
STATIC mp_obj_t st7701s_width(mp_obj_t self_in) {
    st7701s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(st7701s_width_obj, st7701s_width);

// height()
STATIC mp_obj_t st7701s_height(mp_obj_t self_in) {
    st7701s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->height);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(st7701s_height_obj, st7701s_height);

// backlight(on)
STATIC mp_obj_t st7701s_backlight(mp_obj_t self_in, mp_obj_t on_in) {
    st7701s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    
    if (self->backlight >= 0) {
        gpio_set_level(self->backlight, mp_obj_is_true(on_in) ? 1 : 0);
    }
    
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(st7701s_backlight_obj, st7701s_backlight);

// ============================================================================
// Module Registration
// ============================================================================

STATIC const mp_rom_map_elem_t st7701s_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),        MP_ROM_PTR(&st7701s_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_framebuffer), MP_ROM_PTR(&st7701s_framebuffer_obj) },
    { MP_ROM_QSTR(MP_QSTR_width),       MP_ROM_PTR(&st7701s_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height),      MP_ROM_PTR(&st7701s_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_backlight),   MP_ROM_PTR(&st7701s_backlight_obj) },
};
STATIC MP_DEFINE_CONST_DICT(st7701s_locals_dict, st7701s_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    st7701s_type,
    MP_QSTR_ST7701S,
    MP_TYPE_FLAG_NONE,
    make_new, st7701s_make_new,
    locals_dict, &st7701s_locals_dict
);

STATIC const mp_rom_map_elem_t st7701s_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_st7701s) },
    { MP_ROM_QSTR(MP_QSTR_ST7701S),  MP_ROM_PTR(&st7701s_type) },
};
STATIC MP_DEFINE_CONST_DICT(st7701s_module_globals, st7701s_module_globals_table);

const mp_obj_module_t st7701s_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&st7701s_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_st7701s, st7701s_module);