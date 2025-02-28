
import argparse
import os
import sys

def get_symbol_name(filename):
    return os.path.splitext(os.path.basename(filename))[0].split('.')[0]

def file_to_c_include(input_files, output_file, static=True):
    symbols = set()
    
    with open(output_file, "w") as f:
        f.write("#pragma once\n\n")
        
        for input_file in input_files:
            symbol_name = get_symbol_name(input_file)
            if symbol_name in symbols:
                sys.exit(f"Error: Symbol name conflict for '{symbol_name}'")
            symbols.add(symbol_name)
            
            with open(input_file, "rb") as infile:
                data = infile.read()
            
            static_keyword = "static " if static else ""
            
            f.write(f"{static_keyword}const uint8_t {symbol_name}[] = {{\n")
            
            for i in range(0, len(data), 12):  # Format 12 bytes per line
                chunk = ', '.join(f'0x{byte:02X}' for byte in data[i:i+12])
                f.write(f"    {chunk},\n")
            
            f.write("};\n\n")
            f.write(f"#define {symbol_name.upper()}_SIZE {len(data)}\n\n")

def main():
    parser = argparse.ArgumentParser(description="Convert multiple files to a single C include containing uint8_t arrays.")
    parser.add_argument("input_files", nargs='+', help="Paths to the input files.")
    parser.add_argument("output_file", help="Path to the output .h file.")
    parser.add_argument("--no-static", dest="static", action="store_false", help="Make the arrays non-static.")
    
    args = parser.parse_args()
    
    file_to_c_include(args.input_files, args.output_file, args.static)
    
if __name__ == "__main__":
    main()
