"""
ST7701S with framebuf example

The C driver handles hardware only.
All drawing uses MicroPython's built-in framebuf module.
"""

import st7701s
import framebuf
import time

# =============================================================================
# PIN CONFIGURATION - ADJUST FOR YOUR BOARD
# =============================================================================

SPI_CS    = 10
SPI_CLK   = 12
SPI_MOSI  = 11
RESET     = 9
BACKLIGHT = 45  # -1 if not used

PCLK  = 8
HSYNC = 47
VSYNC = 48
DE    = 3

# RGB565 data pins: B0-B4, G0-G5, R0-R4
DATA_PINS = [15, 7, 6, 5, 4, 16, 17, 18, 21, 38, 39, 40, 41, 42, 2, 1]

# =============================================================================
# HELPER: RGB565 color
# =============================================================================

def rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

# Swap bytes for framebuf if needed (try RGB565 first, then RGB565SW)
def rgb565sw(r, g, b):
    """RGB565 byte-swapped (if colors look wrong, try this)"""
    c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((c & 0xFF) << 8) | ((c >> 8) & 0xFF)

# =============================================================================
# COLORS (RGB565)
# =============================================================================

BLACK   = 0x0000
WHITE   = 0xFFFF
RED     = 0xF800
GREEN   = 0x07E0
BLUE    = 0x001F
YELLOW  = 0xFFE0
CYAN    = 0x07FF
MAGENTA = 0xF81F

# =============================================================================
# SETUP
# =============================================================================

print("Creating display...")
display = st7701s.ST7701S(
    SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT,
    PCLK, HSYNC, VSYNC, DE,
    DATA_PINS
)

print("Initializing hardware...")
display.init()

# Wrap framebuffer with framebuf.FrameBuffer
# Try RGB565 first. If colors are wrong, use framebuf.RGB565SW
buf = display.framebuffer()
fb = framebuf.FrameBuffer(buf, display.width(), display.height(), framebuf.RGB565)

print(f"Display ready: {display.width()}x{display.height()}")

# =============================================================================
# DEMO: Basic shapes
# =============================================================================

def demo_basics():
    print("Basic shapes...")
    fb.fill(BLACK)
    
    # Rectangles
    fb.fill_rect(10, 10, 100, 100, RED)
    fb.fill_rect(120, 10, 100, 100, GREEN)
    fb.fill_rect(230, 10, 100, 100, BLUE)
    
    # Outlined rectangle
    fb.rect(10, 120, 460, 100, WHITE)
    
    # Lines
    fb.hline(10, 240, 460, YELLOW)
    fb.vline(240, 250, 200, CYAN)
    
    # Diagonal lines
    fb.line(10, 250, 470, 450, MAGENTA)
    fb.line(470, 250, 10, 450, MAGENTA)
    
    time.sleep(2)

# =============================================================================
# DEMO: Text
# =============================================================================

def demo_text():
    print("Text rendering...")
    fb.fill(BLACK)
    
    # Built-in 8x8 font
    fb.text("ST7701S Display", 10, 10, WHITE)
    fb.text("480 x 854 RGB565", 10, 30, WHITE)
    fb.text("Using framebuf!", 10, 50, GREEN)
    
    # Different colors
    fb.text("RED", 10, 80, RED)
    fb.text("GREEN", 60, 80, GREEN)
    fb.text("BLUE", 130, 80, BLUE)
    fb.text("YELLOW", 190, 80, YELLOW)
    
    # Lots of text
    y = 120
    for i in range(20):
        fb.text(f"Line {i}: The quick brown fox", 10, y, rgb565(255, 255 - i*12, i*12))
        y += 12
    
    time.sleep(2)

# =============================================================================
# DEMO: Pixels
# =============================================================================

def demo_pixels():
    print("Pixel patterns...")
    fb.fill(BLACK)
    
    # Gradient using pixels
    for y in range(200):
        for x in range(480):
            color = rgb565(x // 2, y, (x + y) // 3)
            fb.pixel(x, y + 300, color)
    
    time.sleep(2)

# =============================================================================
# DEMO: Scrolling
# =============================================================================

def demo_scroll():
    print("Scrolling...")
    fb.fill(BLACK)
    
    # Draw some content
    for i in range(20):
        fb.text(f"Scroll line {i}", 10, 100 + i * 20, WHITE)
    
    time.sleep(1)
    
    # Scroll up
    for _ in range(50):
        fb.scroll(0, -5)
        time.sleep(0.05)
    
    time.sleep(1)

# =============================================================================
# DEMO: Ellipses (MicroPython 1.20+)
# =============================================================================

def demo_ellipses():
    print("Ellipses...")
    fb.fill(BLACK)
    
    try:
        # Filled ellipses
        fb.ellipse(120, 200, 80, 50, RED, True)
        fb.ellipse(240, 200, 50, 80, GREEN, True)
        fb.ellipse(360, 200, 60, 60, BLUE, True)
        
        # Outlined ellipses
        fb.ellipse(120, 400, 100, 60, YELLOW, False)
        fb.ellipse(240, 400, 60, 100, CYAN, False)
        fb.ellipse(360, 400, 80, 80, MAGENTA, False)
        
    except AttributeError:
        fb.text("ellipse() not available", 10, 200, RED)
        fb.text("Requires MicroPython 1.20+", 10, 220, WHITE)
    
    time.sleep(2)

# =============================================================================
# DEMO: Animation using blit
# =============================================================================

def demo_blit():
    print("Sprite animation with blit...")
    
    # Create a small sprite (32x32)
    sprite_buf = bytearray(32 * 32 * 2)
    sprite = framebuf.FrameBuffer(sprite_buf, 32, 32, framebuf.RGB565)
    
    # Draw sprite content
    sprite.fill(BLACK)
    sprite.fill_rect(4, 4, 24, 24, RED)
    sprite.fill_rect(8, 8, 16, 16, YELLOW)
    sprite.fill_rect(12, 12, 8, 8, WHITE)
    
    # Animate
    fb.fill(BLUE)
    
    x, y = 0, 0
    dx, dy = 5, 3
    
    for _ in range(200):
        # Clear old position (draw background)
        fb.fill_rect(x, y, 32, 32, BLUE)
        
        # Update position
        x += dx
        y += dy
        
        # Bounce
        if x <= 0 or x >= 480 - 32:
            dx = -dx
        if y <= 0 or y >= 854 - 32:
            dy = -dy
        
        # Draw sprite at new position
        fb.blit(sprite, x, y)
        
        time.sleep(0.02)

# =============================================================================
# MAIN
# =============================================================================

def main():
    while True:
        demo_basics()
        demo_text()
        demo_pixels()
        demo_scroll()
        demo_ellipses()
        demo_blit()
        
        print("\nRestarting demos...\n")

if __name__ == "__main__":
    main()