# ST7701S Minimal Driver

Hardware-only driver for ST7701S RGB LCD on ESP32-S3. Use MicroPython's `framebuf` for drawing.

## C Driver Provides

- `init()` — SPI init sequence + RGB panel setup
- `framebuffer()` — Returns bytearray backed by PSRAM framebuffer
- `width()` / `height()` — Display dimensions (480×854)
- `backlight(on)` — Control backlight

## framebuf Provides

Everything else:

```python
fb.fill(color)
fb.pixel(x, y, color)
fb.hline(x, y, w, color)
fb.vline(x, y, h, color)
fb.line(x1, y1, x2, y2, color)
fb.rect(x, y, w, h, color)
fb.fill_rect(x, y, w, h, color)
fb.text(string, x, y, color)
fb.scroll(dx, dy)
fb.blit(source_fb, x, y)
fb.ellipse(x, y, rx, ry, color)  # MicroPython 1.20+
```

## Usage

```python
import st7701s
import framebuf

# Hardware init
display = st7701s.ST7701S(
    spi_cs, spi_clk, spi_mosi, reset, backlight,
    pclk, hsync, vsync, de, 
    [b0, b1, b2, b3, b4, g0, g1, g2, g3, g4, g5, r0, r1, r2, r3, r4]
)
display.init()

# Wrap with framebuf
fb = framebuf.FrameBuffer(
    display.framebuffer(),
    display.width(),
    display.height(),
    framebuf.RGB565  # or RGB565SW if colors are swapped
)

# Draw!
fb.fill(0x0000)
fb.text("Hello!", 10, 10, 0xFFFF)
fb.fill_rect(100, 100, 200, 200, 0xF800)
```

## Build

```bash
cd micropython/ports/esp32
idf.py -D MICROPY_BOARD=ESP32_GENERIC_S3 \
       -D MICROPY_BOARD_VARIANT=SPIRAM_OCT \
       -D USER_C_MODULES=/path/to/st7701s_minimal/micropython.cmake \
       build
```

## Color Format

RGB565: `color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)`

If red/blue appear swapped, use `framebuf.RGB565SW` instead of `framebuf.RGB565`.