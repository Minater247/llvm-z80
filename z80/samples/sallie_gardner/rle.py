import argparse
import os

"""
Run-Length Encoding (RLE) Scheme:

Control byte encoding:
- End of file: 0
- 0x00 repetitions: 1 + 2 * (24 - length)
- 0xFF repetitions: 1 + 2 * 24 + 3 + 2 * (24 - length)
- Mixed data (raw bytes): 1 + 2 * 24 + 3 + 2 * 24 + 3 + 2 * (12 - length)

Each section is separated by an offset of 3 from the previous control block.
The decoder tests the control range rather than performing calculations.
"""

def run_length_encode(input_file, output_file):
    with open(input_file, 'rb') as f:
        data = f.read()
    
    encoded_data = bytearray()
    i = 0
    while i < len(data):
        if data[i] == 0x00:
            count = 1
            while i + 1 < len(data) and data[i + 1] == 0x00 and count < 24:
                count += 1
                i += 1
            control_byte = 1 + 2 * (24 - count)
            encoded_data.append(control_byte)
        elif data[i] == 0xFF:
            count = 1
            while i + 1 < len(data) and data[i + 1] == 0xFF and count < 24:
                count += 1
                i += 1
            control_byte = 1 + 2 * 24 + 3 + 2 * (24 - count)
            encoded_data.append(control_byte)
        else:
            start = i
            while i < len(data) and data[i] not in (0x00, 0xFF) and (i - start) < 12:
                i += 1
            count = i - start
            control_byte = 1 + 2 * 24 + 3 + 2 * 24 + 3 + 2 * (12 - count)
            encoded_data.append(control_byte)
            encoded_data.extend(data[start:i])
            continue  # Avoid extra increment
        i += 1
    
    # Append end-of-data marker
    encoded_data.append(0)  # End of file marker
    
    with open(output_file, 'wb') as f:
        f.write(encoded_data)
    print(f"Encoded file saved to {output_file}")

def run_length_decode(input_file, output_file):
    with open(input_file, 'rb') as f:
        data = f.read()
    
    decoded_data = bytearray()
    i = 0
    while i < len(data):
        control_byte = data[i]
        i += 1
        
        if control_byte == 0:  # End of file marker
            break
        elif control_byte < (1 + 2 * 24):  # 0x00 block
            length = 24 - (control_byte - 1) // 2
            decoded_data.extend([0x00] * length)
        elif control_byte < (1 + 2 * 24 + 3 + 2 * 24):  # 0xFF block
            length = 24 - (control_byte - (1 + 2 * 24 + 3)) // 2
            decoded_data.extend([0xFF] * length)
        else:  # Mixed data block
            length = 12 - (control_byte - (1 + 2 * 24 + 3 + 2 * 24 + 3)) // 2
            decoded_data.extend(data[i:i+length])
            i += length
    
    with open(output_file, 'wb') as f:
        f.write(decoded_data)
    print(f"Decoded file saved to {output_file}")

def main():
    parser = argparse.ArgumentParser(description="Run-Length Encode or Decode a binary file.")
    parser.add_argument("mode", choices=["encode", "decode"], help="Operation mode: encode or decode")
    parser.add_argument("input_file", help="Path to the input file")
    parser.add_argument("output_file", help="Path to the output file")
    args = parser.parse_args()
    
    if args.mode == "encode":
        run_length_encode(args.input_file, args.output_file)
    elif args.mode == "decode":
        run_length_decode(args.input_file, args.output_file)

if __name__ == "__main__":
    main()

