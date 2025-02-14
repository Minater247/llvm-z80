#!/usr/bin/env python3

import argparse
import struct
import binascii
import os

from tap_utils import *

def parse_hex_file(hex_file_path, verbose=False):
    """ Parses an Intel HEX file and returns the binary data along with the lowest address found. """
    data = bytearray()
    start_address = None
    
    with open(hex_file_path, "r") as f:
        for line in f:
            if not line.startswith(":"):
                continue  # Ignore invalid lines
            
            # Parse record
            length = int(line[1:3], 16)
            address = int(line[3:7], 16)
            record_type = int(line[7:9], 16)
            data_part = line[9:9 + length * 2]
            checksum = int(line[9 + length * 2: 9 + length * 2 + 2], 16)

            if verbose:
                print(f"Record: Address=0x{address:04X}, Length={length}, Type={record_type}")
            
            # Only process data records
            if record_type == 0:
                if start_address is None or address < start_address:
                    start_address = address
                data += binascii.unhexlify(data_part)
    
    if verbose:
        print(f"Total binary data extracted: {len(data)} bytes, Start Address: 0x{start_address:04X}")
    
    return data, start_address

def create_tap_file(binary_data, filename, start_address, verbose=False):
    """ Creates a ZX Spectrum .TAP file from binary data. """
    tap_data = bytearray()
    length = len(binary_data)
    param2 = start_address  # Parameter 2 (usually execution address)

    # Ensure filename is 10 bytes long
    filename = filename.ljust(10)[:10]
    
    # Construct header block
    header = (
        struct.pack("<H", 19) +             # Block length (19 bytes for header)
        b'\x00' +                           # Flag byte (0 = header)
        b'\x03' +                           # File type (3 = CODE)
        filename.encode("ascii") +          # 10-byte filename
        struct.pack("<H", length) +         # Length of data block
        struct.pack("<H", start_address) +  # Start address
        struct.pack("<H", 32768)            # Unused (param2)
    )

    header_checksum = calculate_checksum(header[2:])
    header += bytes([header_checksum])  # Add checksum byte
    tap_data += header
    
    if verbose:
        print(f"TAP Header: Filename={filename.strip()}, Length={length}, Start=0x{start_address:04X}, Checksum=0x{header_checksum:02X}")
    
    # Construct data block
    data_block = (
        struct.pack("<H", length + 2) +  # Block length (data + flag + checksum)
        b'\xFF' +  # Flag byte (255 = data block)
        binary_data  # Program data
    )

    data_checksum = calculate_checksum(data_block[2:])
    data_block += bytes([data_checksum])  # Add checksum byte
    tap_data += data_block
    
    if verbose:
        print(f"TAP Data Block: Length={length}, Checksum=0x{data_checksum:02X}")

    return tap_data

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert an Intel HEX file to a ZX Spectrum .TAP file")
    parser.add_argument("input", help="Input HEX file")
    parser.add_argument("-o", "--output", help="Output TAP file (default: input filename with .tap extension)")
    parser.add_argument("--filename", help="Filename inside TAP (max 10 chars, default: input filename)")
    parser.add_argument("--start", type=lambda x: int(x, 0), help="Start address override (default: first address in HEX file)")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose output")
    parser.add_argument("--include-loader", action="store_true", help="Include loader BASIC program")
    
    args = parser.parse_args()
    
    # Determine default filenames
    input_basename = os.path.splitext(os.path.basename(args.input))[0]
    output_file = args.output if args.output else f"{input_basename}.tap"
    tap_filename = args.filename if args.filename else input_basename[:10]
    
    binary_data, detected_start_address = parse_hex_file(args.input, args.verbose)
    start_address = args.start if args.start is not None else detected_start_address

    if start_address <= 0x4000:
        print('Warning: start address at ROM space: 0x{start_address:04x}')
    elif start_address < 0x6000:
        print('Warning: start address at low value: 0x{start_address:04x}')

    if False:
        key = b'\x21\x00\x00'
        print('key', key)
        if key in binary_data:
            print('found it!')
            binary_data = binary_data.replace(key, b'\x00\x00\x00')
    
    tap_data = create_tap_file(binary_data, tap_filename, start_address, args.verbose)

    if args.include_loader:
        loader_name = ('L' + tap_filename)[:10]
        tap_data = generate_loader_tap(tap_filename, detected_start_address, loader_name) + tap_data

    # Write TAP file
    with open(output_file, "wb") as f:
        f.write(tap_data)
    
    if args.verbose:
        print(f"Successfully created {output_file}")

