import sys
import struct
import tkinter as tk


def rgb565_to_rgb888(pixel):
    r = (pixel >> 11) & 0x1F
    g = (pixel >> 5) & 0x3F
    b = pixel & 0x1F

    r = (r << 3) | (r >> 2)
    g = (g << 2) | (g >> 4)
    b = (b << 3) | (b >> 2)

    return r, g, b


def load_rgb565_as_ppm(filename):
    with open(filename, "rb") as f:
        data = f.read()

    width, height = struct.unpack_from("<HH", data, 0)
    print(width, height)
    pixel_data = data[4:]

    expected = width * height * 2
    if len(pixel_data) < expected:
        raise ValueError("File too small")

    header = f"P6 {width} {height} 255\n".encode()
    pixels = bytearray()

    for i in range(0, expected, 2):
        pixel = struct.unpack_from("<H", pixel_data, i)[0]
        pixels.extend(rgb565_to_rgb888(pixel))

    return header + pixels, width, height


def main():
    if len(sys.argv) != 2:
        print("Usage: python view_rgb565.py <file.rgb565>")
        sys.exit(1)

    filename = sys.argv[1]

    ppm_data, width, height = load_rgb565_as_ppm(filename)

    root = tk.Tk()
    root.title(f"{filename} ({width}x{height})")

    img = tk.PhotoImage(data=ppm_data)
    tk.Label(root, image=img).pack()

    root.mainloop()


if __name__ == "__main__":
    main()
