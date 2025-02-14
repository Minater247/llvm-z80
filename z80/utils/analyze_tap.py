#!/usr/bin/env python3

import argparse
import struct
import os

from tap_utils import calculate_checksum

def read_tap_file(filename):
    try:
        with open(filename, "rb") as f:
            data = f.read()
            return data
    except FileNotFoundError:
        print(f"Error: File '{filename}' not found.")
        exit(1)

def parse_tap_data(data, filename, save_blocks):
    index = 0
    block_count = 0
    base_filename, _ = os.path.splitext(filename)
    
    while index < len(data):
        start_address = index

        if index + 2 > len(data):
            print("Unexpected end of file while reading block length.")
            break
        
        block_length = struct.unpack("<H", data[index:index+2])[0]
        index += 2
        
        if index + block_length > len(data):
            print("Corrupt TAP file: Block length exceeds file size.")
            break
        
        block_data = data[index:index+block_length]
        index += block_length
        
        if block_length < 2:
            print(f"Skipping invalid block at position {index-block_length}")
            continue
        
        block_type = block_data[0]
        expected_checksum = block_data[-1]
        checksum = calculate_checksum(block_data[:-1])
        checksum_status = "(OK)" if checksum == expected_checksum else "(Mismatch)"
        
        print(f"0x{start_address:04x}: Block {block_count}: Flag: {hex(block_type)}, Checksum: {checksum} Expected: {expected_checksum} {checksum_status}")
        
        if checksum != expected_checksum:
            print(f"Warning: Checksum mismatch in block {block_count}.")
        
        if block_type == 0x00:  # Header block
            header = block_data[1:]
            if len(header) < 17:
                print("Invalid header block.")
                continue
            
            file_type = header[0]
            filename = header[1:11].decode('ascii', 'ignore').strip()
            length = struct.unpack("<H", header[11:13])[0]
            param1 = struct.unpack("<H", header[13:15])[0]
            param2 = struct.unpack("<H", header[15:17])[0]
            
            print(f"  File Name: {filename}")
            print(f"  File Type: {file_type} ({get_file_type(file_type)})")
            print(f"  Length: {length}")
            print(f"  Param1: {param1}")
            print(f"  Param2: {param2}")
        else:
            if block_data[0] != 0xFF:
                print(f"Warning: Data block {block_count} is not marked with 0xFF flag. Found: {hex(block_data[0])}")
            
            print(f"  Block Length: {block_length}")
            
            if save_blocks:
                extension = get_file_extension(file_type)
                block_filename = f"{base_filename}.{block_count}{extension}"
                with open(block_filename, "wb") as block_file:
                    block_file.write(block_data[1:-1])
                print(f"  Saved as: {block_filename}")
        
        block_count += 1

def get_file_type(type_id):
    types = {
        0: "BASIC Program",
        1: "Number Array",
        2: "Character Array",
        3: "Code/Data Block"
    }
    return types.get(type_id, "Unknown")

def get_file_extension(type_id):
    extensions = {
        0: ".bas",
        1: ".arr",
        2: ".chr",
        3: ".bin"
    }
    return extensions.get(type_id, ".dat")

def main():
    parser = argparse.ArgumentParser(description="ZX Spectrum TAP file analyzer")
    parser.add_argument("filename", help="Path to the TAP file")
    parser.add_argument("--save-blocks", action="store_true", help="Save data blocks as separate files")
    args = parser.parse_args()
    
    data = read_tap_file(args.filename)
    parse_tap_data(data, args.filename, args.save_blocks)

if __name__ == "__main__":
    main()
