
import struct

def calculate_checksum(data):
    checksum = 0
    for byte in data:
        checksum ^= byte
    return checksum

def generate_loader_tap(program_name, execute_address, loader_name = 'loader'):
    tap_data = bytearray()

    clear_address = execute_address - 1

    clear_address_encoded = struct.pack("<H", clear_address)
    execute_address_encoded = struct.pack("<H", execute_address)

    clear_address_formatted = f'{clear_address}'.encode('ascii')
    execute_address_formatted = f'{execute_address}'.encode('ascii')

    program_name_formatted = program_name.ljust(10)[:10].encode('ascii')

    loader_basic = (
        b'\x00\x01\x11\x00 \xeaGeneral loader\r' +
        b'\x00\x02\x0e\x00 \xfd' + clear_address_formatted + b'\x0e\x00\x00' + clear_address_encoded + b'\x00\r' +
        b'\x00\x03\x1c\x00 \xef"' + program_name_formatted + b'" \xaf' + execute_address_formatted + b'\x0e\x00\x00' + execute_address_encoded + b'\x00\r' +
        b'\x00\x04\x0f\x00 \xf9\xc0' + execute_address_formatted + b'\x0e\x00\x00' + execute_address_encoded + b'\x00\r'
    )

    length = len(loader_basic)

    header = (
        struct.pack("<H", 19) +             # Block length (19 bytes for header)
        b'\x00' +                           # Flag byte (0 = header)
        b'\x00' +                           # File type (3 = BASIC)
        loader_name.ljust(10)[:10].encode("ascii") +          # 10-byte filename
        struct.pack("<H", length) +         # Length of program
        struct.pack("<H", 0) +
        struct.pack("<H", length)           # Repeat length? what is this?
    )

    header_checksum = calculate_checksum(header[2:])
    header += bytes([header_checksum])  # Add checksum byte

    tap_data += header

    # Construct data block
    data_block = (
        struct.pack("<H", length + 2) +  # Block length (data + flag + checksum)
        b'\xFF' +  # Flag byte (255 = data block)
        loader_basic  # Program data
    )

    data_checksum = calculate_checksum(data_block[2:])
    data_block += bytes([data_checksum])  # Add checksum byte
    tap_data += data_block

    return tap_data

