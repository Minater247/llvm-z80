#!/usr/bin/env python3
"""
TRSDOS /CMD File Analyzer

Analyzes and displays the structure of TRSDOS /CMD files,
showing all records with their types, addresses, and content.
"""

import argparse
import struct
import sys
from pathlib import Path

# Record type definitions from the TRSDOS /CMD specification
RECORD_TYPES = {
    0x01: "LOAD",
    0x02: "XFER", 
    0x04: "EOM",
    0x05: "HDR",
    0x06: "PDSHDR",
    0x07: "PATCH",
    0x08: "ISAM",
    0x0A: "EODIR",
    0x0C: "PDSDIR",
    0x0E: "EOPDS",
    0x10: "YANK",
    0x1F: "COPYRIGHT"
}

def analyze_cmd_file(filename: str, show_hex: bool = False, max_hex_bytes: int = 16):
    """Analyze a CMD file and display its structure"""
    
    try:
        with open(filename, 'rb') as f:
            data = f.read()
    except Exception as e:
        print(f"Error reading file '{filename}': {e}", file=sys.stderr)
        return False
    
    if len(data) == 0:
        print(f"File '{filename}' is empty")
        return False
    
    print(f"CMD File Analysis: {filename}")
    print(f"File size: {len(data)} bytes")
    print("=" * 60)
    
    pos = 0
    record_num = 1
    total_load_bytes = 0
    
    while pos < len(data):
        if pos + 1 >= len(data):
            print(f"Error: Truncated record at offset 0x{pos:04X}")
            break
            
        record_type = data[pos]
        record_len = data[pos + 1] if data[pos + 1] != 0 else 256
        
        # Check if we have enough data for this record
        if pos + 2 + record_len > len(data):
            print(f"Error: Record #{record_num} extends beyond file end")
            break
        
        type_name = RECORD_TYPES.get(record_type, f"UNKNOWN(0x{record_type:02X})")
        print(f"Record #{record_num} at offset 0x{pos:04X}:")
        print(f"  Type: 0x{record_type:02X} ({type_name})")
        print(f"  Length: {record_len} bytes")
        
        # Extract and analyze record data
        record_data = data[pos + 2:pos + 2 + record_len]
        
        if record_type == 0x05:  # Module Header
            try:
                name = record_data.decode('ascii').rstrip()
                print(f"  Module name: \"{name}\"")
            except UnicodeDecodeError:
                print(f"  Module name: {record_data.hex()} (non-ASCII)")
                
        elif record_type == 0x01:  # Load Block
            if len(record_data) >= 2:
                addr = struct.unpack('<H', record_data[0:2])[0]
                payload = record_data[2:]
                payload_len = len(payload)
                total_load_bytes += payload_len
                print(f"  Load address: 0x{addr:04X}")
                print(f"  Payload: {payload_len} bytes")
                
                if show_hex and payload_len > 0:
                    hex_bytes = min(max_hex_bytes, payload_len)
                    hex_str = payload[:hex_bytes].hex(' ', 1)
                    if payload_len > hex_bytes:
                        hex_str += "..."
                    print(f"  Data: {hex_str}")
            else:
                print(f"  Error: Load block too short ({len(record_data)} bytes)")
                
        elif record_type == 0x02:  # Transfer Address
            if len(record_data) >= 2:
                addr = struct.unpack('<H', record_data[0:2])[0]
                print(f"  Entry point: 0x{addr:04X}")
            else:
                print(f"  Error: Transfer address record too short")
                
        elif record_type == 0x1F:  # Copyright
            try:
                text = record_data.decode('ascii')
                print(f"  Copyright: \"{text}\"")
            except UnicodeDecodeError:
                print(f"  Copyright: {record_data.hex()} (non-ASCII)")
                
        elif record_type == 0x04:  # End of Member
            print(f"  End of member marker")
            
        else:
            # Unknown or unhandled record type
            if show_hex and len(record_data) > 0:
                hex_bytes = min(max_hex_bytes, len(record_data))
                hex_str = record_data[:hex_bytes].hex(' ', 1)
                if len(record_data) > hex_bytes:
                    hex_str += "..."
                print(f"  Data: {hex_str}")
        
        print()
        pos += 2 + record_len
        record_num += 1
    
    print("=" * 60)
    print(f"Total records: {record_num - 1}")
    print(f"Total load data: {total_load_bytes} bytes")
    
    if pos != len(data):
        print(f"Warning: {len(data) - pos} bytes remaining at end of file")
    
    return True

def main():
    parser = argparse.ArgumentParser(description="Analyze TRSDOS /CMD file structure")
    parser.add_argument("cmd_file", help="CMD file to analyze")
    parser.add_argument("-x", "--hex", action="store_true", 
                       help="Show hex dump of record data")
    parser.add_argument("-b", "--bytes", type=int, default=16, metavar="N",
                       help="Maximum bytes to show in hex dump (default: 16)")
    
    args = parser.parse_args()
    
    if not Path(args.cmd_file).exists():
        print(f"Error: File '{args.cmd_file}' not found", file=sys.stderr)
        return 1
    
    success = analyze_cmd_file(args.cmd_file, args.hex, args.bytes)
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
