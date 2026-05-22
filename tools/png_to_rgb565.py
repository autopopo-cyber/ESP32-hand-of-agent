"""Convert PNG to RGB565 C header for ST7789 172x320 display."""
import sys, os
from PIL import Image

# 帧缓冲尺寸 (swap_xy 后: 320=垂直, 172=水平)
W, H = 320, 172

def png_to_rgb565(path, out_dir):
    img = Image.open(path).convert("RGB")
    # ROTATE_270: 竖版原图 → 横版填 320x172 帧缓冲 (DOWN 基准)
    img = img.transpose(Image.ROTATE_270)

    iw, ih = img.size
    scale = max(W / iw, H / ih)
    nw, nh = int(iw * scale), int(ih * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    # Center crop
    left = (nw - W) // 2
    top = (nh - H) // 2
    img = img.crop((left, top, left + W, top + H))

    pixels = []
    for y in range(H):
        for x in range(W):
            r, g, b = img.getpixel((x, y))
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            inv = ~rgb565 & 0xFFFF
            pixels.append(f"0x{inv:04X}")

    base = os.path.splitext(os.path.basename(path))[0]
    out_path = os.path.join(out_dir, f"{base}.h")
    with open(out_path, "w") as f:
        f.write(f"// Auto-generated from {os.path.basename(path)}\n")
        f.write(f"// Size: {W}x{H}, Format: RGB565\n\n")
        f.write("#pragma once\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define SPLASH_W {W}\n")
        f.write(f"#define SPLASH_H {H}\n\n")
        f.write(f"static const uint16_t splash_image[{W} * {H}] = {{\n")
        for i in range(0, len(pixels), 16):
            f.write("    " + ", ".join(pixels[i:i+16]) + ",\n")
        f.write("};\n")
    print(f"Written: {out_path} ({len(pixels)} pixels, {len(pixels)*2} bytes)")
    return out_path

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.png> <output_dir>")
        sys.exit(1)
    png_to_rgb565(sys.argv[1], sys.argv[2])
