"""
ST7701 Display Example
480x854 RGB LCD on ESP32-S3

Adjust the pin definitions below to match your hardware.
"""

import st7701
import framebuf
import time

# =============================================================================
# PIN CONFIGURATION - ADJUST THESE FOR YOUR BOARD
# =============================================================================

SPI_CS   = 41
SPI_CLK  = 42
SPI_MOSI = 2
RESET    = 1
BACKLIGHT = -1

PCLK  = 40
HSYNC = 5
VSYNC = 38
DE    = 39

DATA_PINS = [12, 47, 21, 14, 4,  11, 10, 9, 3, 8, 18,  7, 17, 16, 15, 13]

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

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
    
    return st7701.rgb565(r, g, b)

# =============================================================================
# DEMO FUNCTIONS
# =============================================================================

def demo_colours(framebuffer):
    """Cycle through basic colours"""
    colours = [
        (st7701.RED, "Red"),
        (st7701.GREEN, "Green"),
        (st7701.BLUE, "Blue"),
        (st7701.WHITE, "White"),
        (st7701.BLACK, "Black"),
        (st7701.rgb565(255, 255, 0), "Yellow"),
        (st7701.rgb565(255, 0, 255), "Magenta"),
        (st7701.rgb565(0, 255, 255), "Cyan"),
        (st7701.rgb565(255, 128, 0), "Orange"),
    ]
    
    print("Colour demo...")
    for colour, name in colours:
        print(f"  {name}")
        framebuffer.fill(colour)
        time.sleep(0.5)

def demo_rectangles(framebuffer, width, height):
    """Draw concentric rectangles"""
    print("Rectangle demo...")
    framebuffer.fill(st7701.BLACK)
    
    colours = [st7701.RED, st7701.GREEN, st7701.BLUE, 
              st7701.WHITE, st7701.rgb565(255, 255, 0)]
    
    for i, colour in enumerate(colours * 2):
        margin = i * 30
        framebuffer.fill_rect(margin, margin, width - margin * 2, height - margin * 2, colour)
        time.sleep(0.2)

def demo_lines(framebuffer, width, height):
    """Draw line patterns"""
    print("Line demo...")
    framebuffer.fill(st7701.BLACK)
    
    # Horizontal lines
    for y in range(0, height, 20):
        hue = (y * 360) // height
        colour = hsv_to_rgb565(hue, 100, 100)
        framebuffer.hline(0, y, width, colour)
    
    time.sleep(2)
    
    # Vertical lines
    framebuffer.fill(st7701.BLACK)
    for x in range(0, width, 10):
        hue = (x * 360) // width
        colour = hsv_to_rgb565(hue, 100, 100)
        framebuffer.vline(x, 0, height, colour)


def demo_gradient(display, width, height):
    """Draw a smooth HSV gradient using direct framebuffer access"""
    print("Gradient demo...")

    # Note that this is the display framebuffer, not framebuf used in the other examples
    fb = display.framebuffer()
    
    for y in range(height):
        # Floating point hue 0.0 - 6.0
        h = (y / (height - 1)) * 6.0
        sector = int(h)
        frac = h - sector
        
        if sector == 0:    
            r, g, b = 1.0, frac, 0.0
        elif sector == 1:
            r, g, b = 1.0 - frac, 1.0, 0.0
        elif sector == 2:
            r, g, b = 0.0, 1.0, frac
        elif sector == 3:
            r, g, b = 0.0, 1.0 - frac, 1.0
        elif sector == 4:
            r, g, b = frac, 0.0, 1.0
        else:
            r, g, b = 1.0, 0.0, 1.0 - frac
        
        # Convert to RGB565
        colour = st7701.rgb565(int(r * 255), int(g * 255), int(b * 255))
        
        # Write entire row directly to the framebuffer (little-endian)
        lo = colour & 0xFF
        hi = (colour >> 8) & 0xFF
        for x in range(width):
            offset = (y * width + x) * 2
            fb[offset] = lo
            fb[offset + 1] = hi
        
        # Progress indicator
        if y % 100 == 0:
            print(f"  {y}/{height}")

def demo_checkerboard(framebuffer, width, height):
    """Draw a checkerboard pattern"""
    print("Checkerboard demo...")
    framebuffer.fill(st7701.BLACK)
    
    square_size = 40
        
    for y in range(0, height, square_size):
        for x in range(0, width, square_size):
            if ((x // square_size) + (y // square_size)) % 2 == 0:
                framebuffer.rect(x, y, square_size, square_size, st7701.WHITE, True)

def demo_pixels(framebuffer, width, height):
    """Draw random pixels (slower, demonstrates pixel() method)"""
    print("Pixel demo...")
    framebuffer.fill(st7701.BLACK)
    
    import random
    
    for _ in range(10000):
        x = random.randint(0, width - 1)
        y = random.randint(0, height - 1)
        colour = hsv_to_rgb565(random.randint(0, 359), 100, 100)
        framebuffer.pixel(x, y, colour)

def demo_blit(framebuffer, width, height):
    """Demonstrate blit with a moving sprite"""
    print("Blit demo...")
    framebuffer.fill(st7701.BLACK)
    
    # Create a small sprite (50x50)
    sprite_w, sprite_h = 50, 50
    sprite = bytearray(sprite_w * sprite_h * 2)
    empty_sprite = bytearray(0xff * sprite_w * sprite_h * 2)
        
    # Fill sprite with gradient
    for y in range(sprite_h):
        for x in range(sprite_w):
            # Create a circular gradient
            dx = x - sprite_w // 2
            dy = y - sprite_h // 2
            dist = (dx * dx + dy * dy) ** 0.5
            
            if dist < sprite_w // 2:
                intensity = int(255 * (1 - dist / (sprite_w // 2)))
                colour = st7701.rgb565(intensity, intensity // 2, 0)
            else:
                colour = 0  # Transparent (black)
            
            offset = (y * sprite_w + x) * 2
            sprite[offset] = colour & 0xFF
            sprite[offset + 1] = (colour >> 8) & 0xFF

    framebuffer.fill(st7701.BLACK)
    
    # Animate sprite
    for frame in range(150):
        # Calculate bouncing position
        x = int((width - sprite_w) * (0.5 + 0.5 * (frame / 50 % 2 - 0.5)))
        y = int((height - sprite_h) * abs((frame % 60) - 30) / 30)

        x = int((width - sprite_w) * abs((frame % 100) - 50) / 50)
        y = int((height - sprite_h) * abs((frame % 60) - 30) / 30)

        # Blit sprite
        framebuffer.blit((sprite, sprite_w, sprite_h, framebuf.RGB565), x, y)
        time.sleep(0.08)
        framebuffer.blit((empty_sprite, sprite_w, sprite_h, framebuf.RGB565), x, y)

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

    # Create a framebuffer
    fb = framebuf.FrameBuffer(display.framebuffer(), display.width(), display.height(), framebuf.RGB565)

    # Run demos
    while True:
        demo_colours(fb)
        time.sleep(2)
        
        demo_rectangles(fb, display.width(), display.height())
        time.sleep(2)
        
        demo_lines(fb, display.width(), display.height())
        time.sleep(2)
        
        demo_gradient(display, display.width(), display.height())
        time.sleep(2)
        
        demo_checkerboard(fb, display.width(), display.height())
        time.sleep(2)
        
        demo_pixels(fb, display.width(), display.height())
        time.sleep(2)
        
        demo_blit(fb, display.width(), display.height())
        time.sleep(2)
        
        print("\nRestarting demos...\n")

if __name__ == "__main__":
    main()
