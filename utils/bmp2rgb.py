from PIL import Image
import struct
import sys

def convert_rgb565(input_file, output_file):
    img = Image.open(input_file).convert("RGB")  # Explicitly use RGB mode
    width, height = img.size

    with open(output_file, "wb") as f:
        # Optional: write header (little-endian width/height)
        f.write(struct.pack("<HH", width, height))

        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y))  # Directly get R, G, B

                # Standard RGB565 packing
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

                # Big-endian 16-bit write
                f.write(struct.pack(">H", rgb565))

    print(f"Saved {output_file} ({width}x{height})")

convert_rgb565(sys.argv[1], sys.argv[2])
