# MicroPython C module for ST7701 LCD displays on ESP32-S3

This Micropython driver is designed as a C user module, to be compiled into the Micropython binary. The driver requires ESP-IDF components (specifically the ESP-IDF [esp_lcd](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/lcd/index.html) module) that must be linked at firmware compile time meaning that we cannot make this into a standalone .mpy file.

This driver is specifically configured for **480(w) x 854(h)** displays with:
- 9-bit SPI for initialization
- 16-bit RGB565 parallel interface for pixel data

For example [this display:](https://www.aliexpress.com/item/1005008239425152.html?spm=a2g0o.order_list.order_list_main.11.4ac71802SVVgz5)

![LCD](/docs/Display.jpg)
<br>
<br>
<br>
## Build Steps

### Clone ESP-IDF
```bash
cd ~
mkdir ~/esp
cd ~/esp
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd ~/esp/esp-idf
./install.sh esp32,esp32s3
```

### Clone Micropython & build mpy-cross
```bash
cd ~
git clone -b v1.27.0 --recursive https://github.com/micropython/micropython.git
cd ~/micropython/mpy-cross/
make
```

### Create the modules directory and copy the files to it
```bash
~
└── modules/
    ├── micropython.cmake
    └── st7701/
        ├── st7701.cmake
        └── st7701.c

```

### Build the Micropython firmware with this module included
```bash
cd ~
. esp/esp-idf/export.sh
cd ~/micropython/ports/esp32
make BOARD=ESP32_GENERIC_S3 BOARD_VARIANT=SPIRAM_OCT submodules
export IDF_TARGET=esp32s3
make BOARD=ESP32_GENERIC_S3 BOARD_VARIANT=SPIRAM_OCT USER_C_MODULES=~/modules/micropython.cmake
```
(Note: If the build fails with `undefined reference to 'abort_'` see [this issue](https://github.com/micropython/micropython/issues/18589))

### Flash the firmware
The previous steps will build `micropython.bin` in `~/micropython/ports/esp32/build-ESP32_GENERIC_S3-SPIRAM_OCT`

`bootloader.bin` is under that in the `bootloader` directory, and `partition-table.bin` is in the `partition_table` directory  

(There is also a combined `firmware.bin` which is the three files above combined into one. If using the combined file, flash it at `0x0000` offset)

You can use [ESPTOOL](https://espressif.github.io/esptool-js/) to flash the firmware. Connect to the chip, select the `micropython.bin` file created above and flash it at `0x10000`  
If necessary, you can also flash `bootloader.bin` at `0x0000` and `partition-table.bin` at `0x8000`  
<br>

## Hardware Requirements

- **ESP32-S3** with PSRAM (8MB recommended)
- **ST7701 LCD** 480x854 with RGB and SPI interfaces
- **26** GPIO pins available

## Pin Connections

### SPI Init Pins (directly bit-banged)
| Function | Description |
|----------|-------------|
| SPI_CS   | Chip select |
| SPI_CLK  | Clock |
| SPI_MOSI | Data (9-bit mode, no MISO needed) |
| RESET    | Hardware reset |
| BACKLIGHT| Backlight enable (optional, use -1 if tied high) |

### RGB Interface Pins
| Function | Description |
|----------|-------------|
| PCLK     | Pixel clock |
| HSYNC    | Horizontal sync |
| VSYNC    | Vertical sync |
| DE       | Data enable |
| D0-D15   | 16-bit data bus (RGB565) |

### RGB565 Data Pin Order

The pinout for the display listed above is as follows:

| Pin No  | Pin Name | Description               | 
|---------|----------|------------
| 1       | LEDA     | Back-light Anode          
| 2       | LEDK     | Back-light Cathode 
| 3       | LEDK     | Back-light Cathode 
| 4       | GND      | Power Ground 
| 5       | VCC      | Power supply for interface logic circuits(2.8V-3.3V) 
| 6       | RST      | Reset input pin 
| 7 - 8   | NC       | NC 
| 9       | SDA      | Serial data input / output bidirectional pin for SPI
| 10      | SCK      | Serial clock input for SPI Interface. 
| 11      | CS       | A chip select signal 
| 12      | PCLK     | Dot clock signal for RGB interface operation 
| 13      | DE       | Data enable signal for RGB interface operation 
| 14      | VS       | Frame synchronizing signal for RGB interface operation 
| 15      | HS       | Line synchronizing signal for RGB interface operation 
| 16 - 21 | R0-R5    | parallel data bus for RGB Interface
| 22 - 27 | G0-G5    | parallel data bus for RGB Interface
| 28 - 33 | B0-B5    | parallel data bus for RGB Interface
| 34      | GND      | Power Ground 
| 35      | TP-INT   | NC 
| 36      | TP-SDA   | NC 
| 37      | TP-SCL   | NC 
| 38      | TP-RST   | NC 
| 39      | TP-VCL   | NC 
| 40      | GND      | Power Ground

Use this diagram as a guide on how to wire the display to an ESP32:

![Circuit](/docs/LCDCircuit.jpg)

The `data_pins` list should be ordered:
```
[R0, R1, R2, R3, R4, G0, G1, G2, G3, G4, G5, B0, B1, B2, B3, B4]
```

Connect:
- R0-R4 to LCD R0-R4 (skip R5)
- G0-G5 to LCD G0-G5
- B0-B4 to LCD B0-B4 (skip B5)

## Usage

Note that you can use the MicroPyhon [Framebuffer](https://docs.micropython.org/en/latest/library/framebuf.html) to do drawing (line, pixel, ellipse etc.) on the display. See the examples below, or the more detailed examples in the [examples](https://github.com/ma261065/st7701/tree/main/examples) directory.

### Basic Example

```python
import st7701

# Define your pin configuration
SPI_CS   = 41
SPI_CLK  = 42
SPI_MOSI = 2
RESET    = 1
BACKLIGHT = -1

PCLK  = 40
HSYNC = 5
VSYNC = 38
DE    = 39

# RGB565 data pins: R0-R4, G0-G5, B0-B4
DATA_PINS = [12, 47, 21, 14, 4,   # Red
             11, 10, 9, 3, 8, 18, # Green
             7, 17, 16, 15, 13]   # Blue

# Create display instance
display = st7701.ST7701(
    SPI_CS, SPI_CLK, SPI_MOSI, RESET, BACKLIGHT,
    PCLK, HSYNC, VSYNC, DE,
    DATA_PINS
)

# Initialize the display
display.init()

# Create a framebuffer
framebuffer = framebuf.FrameBuffer(display.framebuffer(), display.width(), display.height(), framebuf.RGB565)

# Fill screen with colour
framebuffer.fill(st7701.RED)

# Draw a filled rectangle
framebuffer.rect(100, 100, 200, 300, st7701.BLUE, True)

# Draw lines
framebuffer.hline(0, 427, 480, st7701.WHITE)  # Centre horizontal
framebuffer.vline(240, 0, 854, st7701.GREEN)  # Centre vertical

# Set individual pixel
framebuffer.pixel(50, 50, st7701.WHITE)

# Control backlight
framebuffer.backlight(True)   # On
framebuffer.backlight(False)  # Off
```

### Using Custom Colors

```python
# RGB565 color format: RRRRRGGGGGGBBBBB

# Using the helper method
orange = st7701.color565(255, 165, 0)
framebuffer.fill(orange)

purple = st7701.rgb565(128, 0, 128)
framebuffer.fill_rect(0, 0, 100, 100, purple)
```

### Blitting Image Data

```python
# Create a buffer (RGB565, 2 bytes per pixel)
width, height = 100, 100
buf = bytearray(width * height * 2)

# Fill buffer with pattern
for y in range(height):
    for x in range(width):
        offset = (y * width + x) * 2
        color = display.color565(x * 2, y * 2, 128)
        buf[offset] = color & 0xFF
        buf[offset + 1] = (color >> 8) & 0xFF

# Blit to screen
framebuffer.blit(buf, 50, 50, width, height)
```

### Direct Framebuffer Access

```python
# Get memoryview of framebuffer for direct manipulation
# Note: this is the framebuffer of the display not a framebuf instance
fb = display.framebuffer()

# Write directly (faster for bulk operations)
# Note: This is a flat array of bytes (RGB565 little-endian)
# Index calculation: offset = (y * 480 + x) * 2

# Example: Set pixel at (10, 20) to white
x, y = 10, 20
offset = (y * 480 + x) * 2
fb[offset] = 0xFF      # Low byte
fb[offset + 1] = 0xFF  # High byte (white = 0xFFFF)
```

## API Reference

### Constructor
```python
ST7701(spi_cs, spi_clk, spi_mosi, reset, backlight,
        pclk, hsync, vsync, de, data_pins)
```

### Methods

| Method | Description |
|--------|-------------|
| `init()` | Initialize display hardware |
| `backlight(on)` | Control backlight (True/False) |
| `width()` | Get display width (480) |
| `height()` | Get display height (854) |
| `framebuffer()` | Get memoryview of framebuffer |
| `color565(r, g, b)` | Convert RGB888 to RGB565 |
| `rotate(buffer, w, h, angle)` | Rotate the display data |
| `swap_bytes(buffer)` | Swap bytes from big-endian to little-endian |

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `BLACK`  | 0x0000 | Black |
| `WHITE`  | 0xFFFF | White |
| `RED`    | 0xF800 | Red |
| `GREEN`  | 0x07E0 | Green |
| `BLUE`   | 0x001F | Blue |

## Troubleshooting

### Black screen after init
1. Check all pin connections
2. Verify backlight is powered
3. Try adjusting `pclk_hz` (8-16MHz range)
4. Check reset timing

### Display garbage/noise
1. Verify RGB data pin order
2. Check timing parameters (porch values)
3. Ensure PSRAM is enabled in build config

### Colors wrong
1. Check if display expects RGB or BGR order
2. Verify color format (RGB565 vs RGB666)
3. May need to swap red/blue in data pins

### Flickering/tearing
1. Enable tearing effect (TE) pin if available
2. Reduce pixel clock frequency
3. Use double-buffering - modify `num_fbs = 2 (will use more memory)

## Modifying for Different Panels

To adapt for a different ST7701 panel:

1. **Resolution**: Change `LCD_H_RES` and `LCD_V_RES`
2. **Init sequence**: Update `st7701_init_sequence()` with vendor's code
3. **Timing**: Adjust the `timings` struct in `setup_rgb_panel()`

## License

MIT License - Use freely in your projects.





