import st7701
import framebuf
import time

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

display = st7701.ST7701(SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT, PCLK, HSYNC, VSYNC, DE, DATA_PINS)
display.init()

# Create a framebuffer
framebuffer = framebuf.FrameBuffer(display.framebuffer(), display.width(), display.height(), framebuf.RGB565)

# Pre-allocate image buffer once at startup
img_buf = bytearray(display.width() * display.height() * 2 + 4)  # max image + header

def load_image(filename, buffer):
    with open(filename, 'rb') as f:
        # Much quicker to use readinto() here rather than read()
        f.readinto(buffer)
    
    w = buffer[0] | (buffer[1] << 8)
    h = buffer[2] | (buffer[3] << 8)
    print(f"Read image W:{w}, H:{h}")
    
    # Return a memoryview of just the pixel data
    return memoryview(buffer)[4:4 + w * h * 2], w, h

data, w, h = load_image("bliss.raw", img_buf)
dst_w, dst_h = st7701.rotate(data, w, h, 270)
fb.blit((data, dst_w, dst_h, framebuf.RGB565), 0, 0)

time.sleep_ms(10000)

display.deinit()
