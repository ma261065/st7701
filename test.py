"""
ST7701 Minimal Test
Quick sanity check that the display is working.

Run this first before the full example.
"""

import st7701
import framebuf
import time

# =============================================================================
# PIN CONFIGURATION - ADJUST THESE FOR YOUR BOARD
# =============================================================================

# SPI init pins
SPI_CS   = 41
SPI_CLK  = 42
SPI_MOSI = 2
RESET    = 1
BACKLIGHT = -1  # -1 if not used

# RGB control pins
PCLK  = 40
HSYNC = 5
VSYNC = 38
DE    = 39

# RGB565 data pins: B0-B4, G0-G5, R0-R4
DATA_PINS = [12, 47, 21, 14, 4,  11, 10, 9, 3, 8, 18,  7, 17, 16, 15, 13]

# =============================================================================
# TEST
# =============================================================================

from machine import Pin
from neopixel import NeoPixel
pin = Pin(48, Pin.OUT)  # set GPIO48 to output to drive NeoPixels
np = NeoPixel(pin, 1)   # create NeoPixel driver on GPIO48 for 1 pixel
np[0] = (0, 0, 0)       # set the LED to black
np.write()              # write data to all pixels

print("Creating display...")
display = st7701.ST7701(
    SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT,
    PCLK, HSYNC, VSYNC, DE,
    
    DATA_PINS
)

print("Initializing...")
display.init()

# Create a framebuffer
framebuffer = framebuf.FrameBuffer(display.framebuffer(), 480, 854, framebuf.RGB565)
print(f"Size: {display.width()}x{display.height()}")

print("Fill RED...")
framebuffer.fill(st7701.RED)

time.sleep(3)

print("Fill GREEN...")
framebuffer.fill(st7701.GREEN)

time.sleep(3)

print("Fill BLUE...")
framebuffer.fill(st7701.BLUE)

time.sleep(3)

print("Draw white rectangle in center...")
framebuffer.fill(st7701.BLACK)
w, h = display.width(), display.height()
framebuffer.rect(w//4, h//4, w//2, h//2, st7701.WHITE, True)

print("Done! If you see colors, the display is working.")

time.sleep(10)
display.deinit()