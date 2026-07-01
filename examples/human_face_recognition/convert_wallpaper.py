"""Convert JPG/PNG images to 1024x600 RGB565 .bin wallpaper files.

Usage: python convert_wallpaper.py <input_dir> <output_dir>

Output: raw RGB565 binary files (1,228,800 bytes each), no header.
These can be copied to the ESP32's /spiflash/ partition via SD card or other means.
"""

import sys
import os
from PIL import Image
import pillow_avif  # register AVIF handler with PIL

WIDTH = 1024
HEIGHT = 600


def rgb888_to_rgb565(r, g, b):
    """Convert 8-8-8 RGB to 5-6-5 RGB565."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def convert_image(input_path, output_path):
    """Resize & convert one image to RGB565 .bin."""
    img = Image.open(input_path).convert("RGB")
    img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)

    pixels = img.load()
    buf = bytearray(WIDTH * HEIGHT * 2)
    idx = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = pixels[x, y]
            c16 = rgb888_to_rgb565(r, g, b)
            buf[idx] = c16 & 0xFF        # low byte
            buf[idx + 1] = (c16 >> 8)    # high byte
            idx += 2

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(buf)
    print(f"  -> {output_path}  ({len(buf)} bytes)")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: python {sys.argv[0]} <input_dir> <output_dir>")
        print(f"Example: python {sys.argv[0]} D:/123/参考 D:/works/esp-who-master/examples/human_face_recognition/wallpapers")
        sys.exit(1)

    input_dir = sys.argv[1]
    output_dir = sys.argv[2]

    if not os.path.isdir(input_dir):
        print(f"Error: input directory not found: {input_dir}")
        sys.exit(1)

    files = sorted([
        f for f in os.listdir(input_dir)
        if f.lower().endswith((".jpg", ".jpeg", ".png", ".bmp"))
    ])

    if not files:
        print(f"No image files found in {input_dir}")
        sys.exit(1)

    print(f"Converting {len(files)} images from {input_dir} to {output_dir}/")
    print(f"Target: {WIDTH}x{HEIGHT} RGB565 raw (.bin)\n")

    for fname in files:
        input_path = os.path.join(input_dir, fname)
        base = os.path.splitext(fname)[0]
        output_path = os.path.join(output_dir, f"{base}.bin")
        print(f"Processing: {fname}")
        convert_image(input_path, output_path)

    print(f"\nDone! Copy the .bin files to ESP32's /spiflash/ partition.")


if __name__ == "__main__":
    main()
