#!/usr/bin/env python3
"""Convert PNG folder to raw RGB565 storage image for ESP32S3 ODA HID Bridge.

Output format:
  4 bytes: magic    = 0x4F444148 ("ODAH")
  4 bytes: image_count (uint32 LE)
  4 bytes: image_w      (uint32 LE, always 172 for ST7789)
  4 bytes: image_h      (uint32 LE, always 320)
  N * (image_w * image_h * 2) bytes: raw RGB565 pixel data
"""

import os
import sys
import struct
from PIL import Image


def png_to_rgb565(img):
    """Convert Pillow image to RGB565 bytes (320x172, rotated for framebuffer)."""
    img = img.transpose(Image.ROTATE_270)
    iw, ih = img.size
    scale = max(320 / iw, 172 / ih)
    nw, nh = int(iw * scale), int(ih * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    left = (nw - 320) // 2
    top = (nh - 172) // 2
    img = img.crop((left, top, left + 320, top + 172))
    if img.mode != 'RGB':
        img = img.convert('RGB')

    data = bytearray(320 * 172 * 2)
    pixels = img.load()
    for y in range(172):
        for x in range(320):
            r, g, b = pixels[x, y]
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            off = (y * 320 + x) * 2
            data[off] = rgb565 & 0xFF
            data[off + 1] = (rgb565 >> 8) & 0xFF
    return bytes(data)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <assets_dir> [output.bin]")
        sys.exit(1)

    assets_dir = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else "storage.bin"

    pngs = sorted([f for f in os.listdir(assets_dir) if f.lower().endswith('.png')])
    if not pngs:
        print("No PNG files found", file=sys.stderr)
        sys.exit(1)

    print(f"Found {len(pngs)} PNGs")

    with open(output, 'wb') as f:
        # Header
        f.write(struct.pack('<I', 0x4F444148))  # magic "ODAH"
        f.write(struct.pack('<I', len(pngs)))     # count
        f.write(struct.pack('<I', 320))           # width
        f.write(struct.pack('<I', 172))           # height

        for name in pngs:
            path = os.path.join(assets_dir, name)
            img = Image.open(path)
            rgb565 = png_to_rgb565(img)
            f.write(rgb565)
            print(f"  {name} ({len(rgb565)} bytes)")

    size_mb = os.path.getsize(output) / (1024 * 1024)
    print(f"Written: {output} ({size_mb:.2f} MB, {len(pngs)} images)")


if __name__ == '__main__':
    main()
