"""
ST7701S Minimal Test with framebuf
"""

import st7701s
import framebuf

# Pin config - ADJUST THESE
SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT = 10, 12, 11, 9, 45
PCLK, HSYNC, VSYNC, DE = 8, 47, 48, 3
DATA_PINS = [15, 7, 6, 5, 4, 16, 17, 18, 21, 38, 39, 40, 41, 42, 2, 1]

# Init hardware
display = st7701s.ST7701S(
    SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT,
    PCLK, HSYNC, VSYNC, DE, DATA_PINS
)
display.init()

# Wrap with framebuf
fb = framebuf.FrameBuffer(
    display.framebuffer(),
    display.width(),
    display.height(),
    framebuf.RGB565
)

# Test
fb.fill(0xF800)  # Red
print("Red - if you see blue, use framebuf.RGB565SW")

import time
time.sleep(1)

fb.fill(0x07E0)  # Green
print("Green")
time.sleep(1)

fb.fill(0x001F)  # Blue
print("Blue - if you see red, use framebuf.RGB565SW")
time.sleep(1)

fb.fill(0x0000)  # Black
fb.text("Hello ST7701S!", 10, 10, 0xFFFF)
fb.text("framebuf works!", 10, 30, 0xFFFF)
fb.rect(5, 5, 200, 50, 0xFFFF)
print("Done!")