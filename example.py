"""
ST7701 Display Example
480x854 RGB LCD on ESP32-S3

Adjust the pin definitions below to match your hardware.
"""

import st7701
import time

# =============================================================================
# PIN CONFIGURATION - ADJUST THESE FOR YOUR BOARD
# =============================================================================

# SPI init pins (directly bit-banged, any GPIO will work)
SPI_CS   = 10
SPI_CLK  = 12
SPI_MOSI = 11
RESET    = 9
BACKLIGHT = 45  # Set to -1 if backlight is always on

# RGB control pins
PCLK  = 8
HSYNC = 47
VSYNC = 48
DE    = 3

# RGB565 data pins in order: B0-B4, G0-G5, R0-R4 (16 pins total)
# These should be grouped physically for cleaner routing
DATA_PINS = [
    # Blue (5 bits)
    15, 7, 6, 5, 4,
    # Green (6 bits)  
    16, 17, 18, 21, 38, 39,
    # Red (5 bits)
    40, 41, 42, 2, 1
]

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

def rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def hsv_to_rgb565(h, s, v):
    """Convert HSV to RGB565 (h: 0-360, s: 0-100, v: 0-100)"""
    s = s / 100
    v = v / 100
    c = v * s
    x = c * (1 - abs((h / 60) % 2 - 1))
    m = v - c
    
    if h < 60:
        r, g, b = c, x, 0
    elif h < 120:
        r, g, b = x, c, 0
    elif h < 180:
        r, g, b = 0, c, x
    elif h < 240:
        r, g, b = 0, x, c
    elif h < 300:
        r, g, b = x, 0, c
    else:
        r, g, b = c, 0, x
    
    r = int((r + m) * 255)
    g = int((g + m) * 255)
    b = int((b + m) * 255)
    
    return rgb565(r, g, b)

# =============================================================================
# DEMO FUNCTIONS
# =============================================================================

def demo_colors(display):
    """Cycle through basic colors"""
    colors = [
        (st7701.RED, "Red"),
        (st7701.GREEN, "Green"),
        (st7701.BLUE, "Blue"),
        (st7701.WHITE, "White"),
        (st7701.BLACK, "Black"),
        (rgb565(255, 255, 0), "Yellow"),
        (rgb565(255, 0, 255), "Magenta"),
        (rgb565(0, 255, 255), "Cyan"),
        (rgb565(255, 128, 0), "Orange"),
    ]
    
    print("Color demo...")
    for color, name in colors:
        print(f"  {name}")
        display.fill(color)
        time.sleep(0.5)

def demo_rectangles(display):
    """Draw concentric rectangles"""
    print("Rectangle demo...")
    display.fill(st7701.BLACK)
    
    w, h = display.width(), display.height()
    colors = [st7701.RED, st7701.GREEN, st7701.BLUE, 
              st7701.WHITE, rgb565(255, 255, 0)]
    
    for i, color in enumerate(colors):
        margin = i * 30
        display.fill_rect(margin, margin, 
                         w - margin * 2, h - margin * 2, color)
        time.sleep(0.2)

def demo_lines(display):
    """Draw line patterns"""
    print("Line demo...")
    display.fill(st7701.BLACK)
    
    w, h = display.width(), display.height()
    
    # Horizontal lines
    for y in range(0, h, 20):
        hue = (y * 360) // h
        color = hsv_to_rgb565(hue, 100, 100)
        display.hline(0, y, w, color)
    
    time.sleep(1)
    
    # Vertical lines
    display.fill(st7701.BLACK)
    for x in range(0, w, 10):
        hue = (x * 360) // w
        color = hsv_to_rgb565(hue, 100, 100)
        display.vline(x, 0, h, color)

def demo_gradient(display):
    """Draw a gradient using direct framebuffer access"""
    print("Gradient demo...")
    
    w, h = display.width(), display.height()
    fb = display.framebuffer()
    
    for y in range(h):
        hue = (y * 360) // h
        color = hsv_to_rgb565(hue, 100, 100)
        
        # Write entire row
        for x in range(w):
            offset = (y * w + x) * 2
            fb[offset] = color & 0xFF
            fb[offset + 1] = (color >> 8) & 0xFF
        
        # Progress indicator
        if y % 100 == 0:
            print(f"  {y}/{h}")

def demo_checkerboard(display):
    """Draw a checkerboard pattern"""
    print("Checkerboard demo...")
    display.fill(st7701.BLACK)
    
    square_size = 40
    w, h = display.width(), display.height()
    
    for y in range(0, h, square_size):
        for x in range(0, w, square_size):
            if ((x // square_size) + (y // square_size)) % 2 == 0:
                display.fill_rect(x, y, square_size, square_size, st7701.WHITE)

def demo_pixels(display):
    """Draw random pixels (slower, demonstrates pixel() method)"""
    print("Pixel demo...")
    display.fill(st7701.BLACK)
    
    import random
    w, h = display.width(), display.height()
    
    for _ in range(5000):
        x = random.randint(0, w - 1)
        y = random.randint(0, h - 1)
        color = hsv_to_rgb565(random.randint(0, 359), 100, 100)
        display.pixel(x, y, color)

def demo_blit(display):
    """Demonstrate blit with a moving sprite"""
    print("Blit demo...")
    display.fill(st7701.BLACK)
    
    # Create a small sprite (50x50)
    sprite_w, sprite_h = 50, 50
    sprite = bytearray(sprite_w * sprite_h * 2)
    
    # Fill sprite with gradient
    for y in range(sprite_h):
        for x in range(sprite_w):
            # Create a circular gradient
            dx = x - sprite_w // 2
            dy = y - sprite_h // 2
            dist = (dx * dx + dy * dy) ** 0.5
            
            if dist < sprite_w // 2:
                intensity = int(255 * (1 - dist / (sprite_w // 2)))
                color = rgb565(intensity, intensity // 2, 0)
            else:
                color = 0  # Transparent (black)
            
            offset = (y * sprite_w + x) * 2
            sprite[offset] = color & 0xFF
            sprite[offset + 1] = (color >> 8) & 0xFF
    
    # Animate sprite
    w, h = display.width(), display.height()
    for frame in range(100):
        # Clear previous position
        display.fill(st7701.BLACK)
        
        # Calculate bouncing position
        x = int((w - sprite_w) * (0.5 + 0.5 * (frame / 50 % 2 - 0.5)))
        y = int((h - sprite_h) * abs((frame % 60) - 30) / 30)
        
        # Blit sprite
        display.blit(sprite, x, y, sprite_w, sprite_h)
        time.sleep(0.03)

# =============================================================================
# MAIN
# =============================================================================

def main():
    print("ST7701 Display Demo")
    print("=" * 40)
    
    # Create display instance
    print("Creating display instance...")
    display = st7701.ST7701(
        SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT,
        PCLK, HSYNC, VSYNC, DE,
        DATA_PINS
    )
    
    # Initialize
    print("Initializing display...")
    display.init()
    
    print(f"Display size: {display.width()}x{display.height()}")
    print()
    
    # Run demos
    while True:
        demo_colors(display)
        time.sleep(1)
        
        demo_rectangles(display)
        time.sleep(1)
        
        demo_lines(display)
        time.sleep(1)
        
        demo_gradient(display)
        time.sleep(1)
        
        demo_checkerboard(display)
        time.sleep(1)
        
        demo_pixels(display)
        time.sleep(1)
        
        demo_blit(display)
        time.sleep(1)
        
        print("\nRestarting demos...\n")

if __name__ == "__main__":
    main()