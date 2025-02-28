import argparse
import os

def run_length_encode(input_file, output_file):
    with open(input_file, 'rb') as f:
        data = f.read()
    
    encoded_data = bytearray()
    i = 0
    while i < len(data):
        if data[i] == 0x00 or data[i] == 0xFF:
            value = data[i]
            count = 1
            while i + 1 < len(data) and data[i] == data[i + 1] and count < 32:
                count += 1
                i += 1
            control_byte = ((2 if value == 0x00 else 1) << 6) | (32 - count + 1)
            encoded_data.append(control_byte)
        else:
            start = i
            while i < len(data) and data[i] not in (0x00, 0xFF) and (i - start) < 32:
                i += 1
            count = i - start
            control_byte = (32 - count + 1)
            encoded_data.append(control_byte)
            encoded_data.extend(data[start:i])
            continue  # Avoid extra increment
        i += 1
    
    # Append end-of-data marker
    encoded_data.append(0b01000000)  # Second bit set, length part = 0
    
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
        if control_byte == 0b01000000:  # End-of-data marker
            break
        mode = (control_byte >> 6) & 0x03
        length = 32 - ((control_byte & 0x3F) - 1)
        i += 1
        
        if mode == 1:
            decoded_data.extend([0xFF] * length)
        elif mode == 2:
            decoded_data.extend([0x00] * length)
        else:
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

