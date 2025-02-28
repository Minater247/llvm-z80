import argparse
import os
import struct
from PIL import Image

def read_bin_file(input_file):
    """Reads a binary file and returns its contents as a byte array."""
    with open(input_file, "rb") as f:
        raw_data = f.read()
    if len(raw_data) != 6144:
        raise ValueError(f"Binary file size is {len(raw_data)} bytes, expected 6144 bytes.")
    return raw_data

def read_png_file(input_file):
    """Reads a PNG file and converts it into a packed 1-bit format for ZX Spectrum."""
    img = Image.open(input_file).convert("1")  # Convert to 1-bit grayscale
    width, height = img.size

    if width != 256 or height != 192:
        raise ValueError(f"PNG image must be 256x192 pixels, but got {width}x{height}.")

    packed_data = bytearray()

    # Convert 1-bit pixels into ZX Spectrum’s 8-pixel-per-byte format
    for y in range(height):
        row = []
        for x in range(0, width, 8):
            byte = 0
            for bit in range(8):
                pixel = img.getpixel((x + bit, y))  # Get pixel (0=black, 255=white)
                if pixel == 0:
                    byte |= (1 << (7 - bit))  # Set bit
            row.append(byte)
        packed_data.extend(row)

    return packed_data

def zx_spectrum_reorder(data):
    """Reorders 256x192 packed data to ZX Spectrum screen memory layout."""
    width = 256
    height = 192
    bytes_per_row = width // 8  # 32 bytes per row
    screen_size = bytes_per_row * height  # 6144 bytes
    zx_data = bytearray(screen_size)

    for y in range(height):
        zx_y = ((y & 0xC0) << 5)+((y & 0x07) << 8)+((y & 0x38) << 2)

        zx_data[zx_y:zx_y + bytes_per_row] = data[y * bytes_per_row:(y + 1) * bytes_per_row]

    return zx_data

def main():
    parser = argparse.ArgumentParser(description="Reorder binary or PNG image to ZX Spectrum video memory format.")
    parser.add_argument("input", help="Input file (either .bin or .png, 256x192)")
    parser.add_argument("output", help="Output binary file (ZX Spectrum format)")

    args = parser.parse_args()

    input_ext = os.path.splitext(args.input)[1].lower()

    if input_ext == ".bin":
        raw_data = read_bin_file(args.input)
    elif input_ext == ".png":
        raw_data = read_png_file(args.input)
    else:
        raise ValueError("Unsupported file type. Please use a .bin or .png file.")

    reordered_data = zx_spectrum_reorder(raw_data)

    with open(args.output, "wb") as f:
        f.write(reordered_data)

    print(f"Successfully converted {args.input} to ZX Spectrum format and saved as {args.output}.")

if __name__ == "__main__":
    main()
