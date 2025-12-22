"""
ST7701 Minimal Test
Quick sanity check that the display is working.

Run this first before the full example.
"""

import st7701

# =============================================================================
# PIN CONFIGURATION - ADJUST THESE FOR YOUR BOARD
# =============================================================================

# SPI init pins
SPI_CS   = 10
SPI_CLK  = 12
SPI_MOSI = 11
RESET    = 9
BACKLIGHT = 45  # -1 if not used

# RGB control pins
PCLK  = 8
HSYNC = 47
VSYNC = 48
DE    = 3

# RGB565 data pins: B0-B4, G0-G5, R0-R4
DATA_PINS = [15, 7, 6, 5, 4, 16, 17, 18, 21, 38, 39, 40, 41, 42, 2, 1]

# =============================================================================
# TEST
# =============================================================================

print("Creating display...")
display = st7701.ST7701(
    SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT,
    PCLK, HSYNC, VSYNC, DE,
    DATA_PINS
)

print("Initializing...")
display.init()

print(f"Size: {display.width()}x{display.height()}")

print("Fill RED...")
display.fill(st7701.RED)

import time
time.sleep(1)

print("Fill GREEN...")
display.fill(st7701.GREEN)

time.sleep(1)

print("Fill BLUE...")
display.fill(st7701.BLUE)

time.sleep(1)

print("Draw white rectangle in center...")
display.fill(st7701.BLACK)
w, h = display.width(), display.height()
display.fill_rect(w//4, h//4, w//2, h//2, st7701.WHITE)

print("Done! If you see colors, the display is working.")
