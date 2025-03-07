import argparse
import os

MAX_FF_COUNT = 7        # number of consequent 0xff packed
MAX_ZZ_COUNT = 8        # number of consequent 0x00 packed
MAX_DATA_COUNT = 10     # number of consequent bytes packed
MAX_SKIP_COUNT = 21     # number of consequent skips done with inc
MIN_REPEAT_COUNT = 3
MAX_REPEAT_COUNT = 8
INLINE_FF = 2
INLINE_ZZ = 3

jump_ret = None
jump_ff = {}
jump_00 = {}
jump_dd = {}
jump_skip = {}
jump_repeat = {}
jump_DD1_PDATA1 = None
jump_DD2_PDATA1 = None
jump_DD1_PDATA2 = None
jump_DD2_PDATA2 = None
jump_DD1_PDATA3 = None

def initialize():
    prefix = f"""        .section .text.function._rle_decode, "ax"
        .global _rle_decode
_rle_decode:
        ld      iy, .decode_loop
        push    ix
        ld      ix, .apply_skip
        jp      (iy)
.apply_skip:
        add     a, e                ; 1 byte, 4 t-states
        ld      e, a                ; 1 byte, 4 t-states
        jp      nc, .decode_loop    ; 3 bytes, 10 t-states
        inc     d                   ; 1 byte, 4 t-states
        jp      (iy)                ; 2 bytes, 8 t-states"""

    pre_index = -7
    post_index = 3

    body = f""".decode_loop:
        ld      a, (hl)             ; Load control byte from source, 1 byte, 7 t-states
        inc     hl                  ; Advance source pointer, 1 byte, 6 t-states
        ld      (.jump + 1), a      ; 3 bytes, 13 t-states
.jump:
        jr      .post_jump          ; 2 bytes
.post_jump:
        pop     ix                  ; 0 RET
        ret                         ;
.body_end:
        .if (.body_end - .post_jump) != {post_index}
            .error "ret offset miscalculated"
        .endif
        .if (.decode_loop - .post_jump) != {pre_index}
            .error "offset miscalculated"
        .endif\n"""

    jump_ret = 0

    for i in range(1, 8):
        pre_index -= 1
        assert pre_index >= -128
        assert jump_skip.get(i) is None
        jump_skip[i] = pre_index
        body = f"""        inc     de                 ; {pre_index:3d} -- {i}\n""" + body

    for i in range(8, MAX_SKIP_COUNT + 1):
        pre_index -= 4
        assert pre_index >= -128
        assert jump_skip.get(i) is None
        jump_skip[i] = pre_index
        body = f"""        ld      a, {i}             ; {pre_index:3d} -- {i}
        jp      (ix)\n""" + body

    # add sanity check
    body = ".first_skip:\n" + body
    body = body + f"""        .if (.first_skip - .post_jump) != {pre_index}
            .error "offset miscalculated"
        .endif\n"""

    # jump_DD1_PDATA1
    pre_index -= 5
    assert pre_index >= -128
    global jump_DD1_PDATA1
    jump_DD1_PDATA1 = pre_index
    tmp  = f""".jump_DD1_PDATA1:\n"""
    tmp += f"""        ldi                        ; {pre_index:3d} jump_DD1_PDATA1\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        jp       (iy)\n"""
    body = tmp + body
    body += f"""        .if (.jump_DD1_PDATA1 - .post_jump) != {pre_index}\n"""
    body += f"""            .error "offset miscalculated"\n"""
    body += f"""        .endif\n"""

    # jump_DD2_PDATA1
    pre_index -= 7
    assert pre_index >= -128
    global jump_DD2_PDATA1
    jump_DD2_PDATA1 = pre_index
    tmp  = f""".jump_DD2_PDATA1:\n"""
    tmp += f"""        ldi                        ; {pre_index:3d} jump_DD2_PDATA1\n"""
    tmp += f"""        ldi\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        jp       (iy)\n"""
    body = tmp + body
    body += f"""        .if (.jump_DD2_PDATA1 - .post_jump) != {pre_index}\n"""
    body += f"""            .error "offset miscalculated"\n"""
    body += f"""        .endif\n"""

    # jump_DD1_PDATA2
    pre_index -= 6
    assert pre_index >= -128
    global jump_DD1_PDATA2
    jump_DD1_PDATA2 = pre_index
    tmp  = f""".jump_DD1_PDATA2:\n"""
    tmp += f"""        ldi                        ; {pre_index:3d} jump_DD1_PDATA2\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        jp       (iy)\n"""
    body = tmp + body
    body += f"""        .if (.jump_DD1_PDATA2 - .post_jump) != {pre_index}\n"""
    body += f"""            .error "offset miscalculated"\n"""
    body += f"""        .endif\n"""

    # jump_DD2_PDATA2
    pre_index -= 8
    assert pre_index >= -128
    global jump_DD2_PDATA2
    jump_DD2_PDATA2 = pre_index
    tmp  = f""".jump_DD2_PDATA2:\n"""
    tmp += f"""        ldi                        ; {pre_index:3d} jump_DD2_PDATA2\n"""
    tmp += f"""        ldi\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        jp       (iy)\n"""
    body = tmp + body
    body += f"""        .if (.jump_DD2_PDATA2 - .post_jump) != {pre_index}\n"""
    body += f"""            .error "offset miscalculated"\n"""
    body += f"""        .endif\n"""

    # jump_DD1_PDATA3
    pre_index -= 7
    assert pre_index >= -128
    global jump_DD1_PDATA3
    jump_DD1_PDATA3 = pre_index
    tmp  = f""".jump_DD1_PDATA3:\n"""
    tmp += f"""        ldi                        ; {pre_index:3d} jump_DD1_PDATA3\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        jp       (iy)\n"""
    body = tmp + body
    body += f"""        .if (.jump_DD1_PDATA3 - .post_jump) != {pre_index}\n"""
    body += f"""            .error "offset miscalculated"\n"""
    body += f"""        .endif\n"""

    # jump_DD1_ZZ_2
    pre_index -= 9
    assert pre_index >= -128
    global jump_DD1_ZZ2
    jump_DD1_ZZ2 = pre_index
    tmp  = f""".jump_DD1_ZZ2:\n"""
    tmp += f"""        ldi                        ; {pre_index:3d} jump_DD1_ZZ2\n"""
    tmp += f"""        xor      a\n"""
    tmp += f"""        ld       (de), a\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        ld       (de), a\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        jp       (iy)\n"""
    body = tmp + body
    body += f"""        .if (.jump_DD1_ZZ2 - .post_jump) != {pre_index}\n"""
    body += f"""            .error "offset miscalculated"\n"""
    body += f"""        .endif\n"""

    # jump_DD1_FF_2
    pre_index -= 10
    assert pre_index >= -128
    global jump_DD1_FF2
    jump_DD1_FF2 = pre_index
    tmp  = f""".jump_DD1_FF2:\n"""
    tmp += f"""        ldi                        ; {pre_index:3d} jump_DD1_FF2\n"""
    tmp += f"""        ld       a, 0xff\n"""
    tmp += f"""        ld       (de), a\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        ld       (de), a\n"""
    tmp += f"""        inc      de\n"""
    tmp += f"""        jp       (iy)\n"""
    body = tmp + body
    body += f"""        .if (.jump_DD1_FF2 - .post_jump) != {pre_index}\n"""
    body += f"""            .error "offset miscalculated"\n"""
    body += f"""        .endif\n"""

    pre_index -= 4
    assert pre_index >= -128
    assert 16 not in jump_00
    jump_00[16] = pre_index
    tmp  = f"""        xor     a                   ; {post_index:3d} 0x00 {16}\n"""
    tmp += f"""        jp      .ld_a_16\n"""
    body = tmp + body

    for i in reversed(range(1, INLINE_FF + 1)):
        assert post_index <= 127
        assert jump_ff.get(i) is None
        jump_ff[i] = post_index

        body += f"""        ld      a, 0xff             ; {post_index:3d} 0xFF {i}
        ld      (de), a
        inc     de\n"""
        post_index += 4

    body += "        jp      (iy)\n"
    post_index += 2

    for i in range(INLINE_FF + 1, MAX_FF_COUNT + 1):
        assert post_index <= 127
        assert jump_ff.get(i) is None
        jump_ff[i] = post_index
        body += f"""        ld      a, 0xff             ; {post_index:3d} 0xFF {i}
        jp      .ld_a_{i}\n"""
        post_index += 5
    body += f""".last_ff:
        .if (.last_ff - .post_jump) != {post_index}
            .error "offset miscalculated"
        .endif\n"""

    assert post_index <= 127
    assert jump_00.get(1) is None
    jump_00[1] = post_index
    body += f"""        xor     a                   ; {post_index:3d} 0x00 {1}\n"""
    body += f"""        ld      (de), a\n"""
    body += f"""        inc     de\n"""
    body += f"""        jp      (iy)\n"""
    post_index += 5

    assert post_index <= 127
    assert jump_00.get(2) is None
    jump_00[2] = post_index
    body += f"""        xor     a                   ; {post_index:3d} 0x00 {2}\n"""
    body += f"""        ld      (de), a\n"""
    body += f"""        inc     de\n"""
    body += f"""        ld      (de), a\n"""
    body += f"""        inc     de\n"""
    body += f"""        jp      (iy)\n"""
    post_index += 7

    assert post_index <= 127
    assert jump_00.get(3) is None
    jump_00[3] = post_index
    body += f"""        xor     a                   ; {post_index:3d} 0x00 {3}\n"""
    body += f"""        ld      (de), a\n"""
    body += f"""        inc     de\n"""
    body += f"""        ld      (de), a\n"""
    body += f"""        inc     de\n"""
    body += f"""        ld      (de), a\n"""
    body += f"""        inc     de\n"""
    body += f"""        jp      (iy)\n"""
    post_index += 9

    for i in range(INLINE_ZZ + 1, MAX_ZZ_COUNT + 1):
        assert post_index <= 127
        assert jump_00.get(i) is None
        jump_00[i] = post_index
        body += f"""        xor     a                   ; {post_index:3d} 0x00 {i}
        jp      .ld_a_{i}\n"""
        post_index += 4
    body += f""".last_00:
        .if (.last_00 - .post_jump) != {post_index}
            .error "offset miscalculated"
        .endif\n"""

    for i in reversed(range(1, MAX_DATA_COUNT + 1)):
        assert post_index <= 127
        assert jump_dd.get(i) is None
        jump_dd[i] = post_index
        body += f"""        ldi                         ; {post_index:3d} DD {i}\n"""
        post_index += 2

    body += "        jp      (iy)\n"
    post_index += 2

    body += f""".last_dd:
        .if (.last_dd - .post_jump) != {post_index}
            .error "offset miscalculated"
        .endif\n"""

    for i in range(MIN_REPEAT_COUNT, MAX_REPEAT_COUNT + 1):
        assert post_index <= 127
        assert jump_repeat.get(i) is None
        jump_repeat[i] = post_index
        body += f"""        ld       a, (hl)            ; {post_index:3d} RR {i}\n"""
        body += f"""        inc      hl\n"""
        body += f"""        jp       .ld_a_{i}\n"""
        post_index += 5
    body += f""".last_rr:
        .if (.last_rr - .post_jump) != {post_index}
            .error "offset miscalculated"
        .endif\n"""

    for i in reversed(range(1, max(MAX_REPEAT_COUNT + 1, MAX_FF_COUNT + 1, MAX_ZZ_COUNT + 1, 16 + 1))):
        body += f""".ld_a_{i}:\n"""
        body += f"""        ld       (de), a\n"""
        body += f"""        inc      de\n"""
    body += f"""        jp       (iy)\n"""

    body = prefix + "\n" + body
    return body

decoder_asm = initialize()

def ff_code(count):
    ret = jump_ff.get(count)
    assert ret is not None
    return ret

def zz_code(count):
    ret = jump_00.get(count)
    assert ret is not None
    return ret

def data_code(count):
    ret = jump_dd.get(count)
    assert ret is not None
    return ret

def skip_code(count):
    ret = jump_skip.get(count)
    assert ret is not None
    return ret

def repeat_code(count):
    ret = jump_repeat.get(count)
    assert ret is not None
    return ret

def add_cmd(cmd):
    global commands
    commands.append(cmd)

def run_length_encode(input_file, output_file, previous_frame_file=None, debug=False):
    global commands
    commands = ["START"]

    with open(input_file, 'rb') as f:
        data = f.read()

    pdata = None
    if previous_frame_file is not None:
        with open(previous_frame_file, 'rb') as f:
            pdata = f.read()
        assert len(data) == len(pdata)

    pdata_break_count = 1
    encoded_data = bytearray()
    i = 0
    while i < len(data):
        # Lets try to fold the last two commands
        if len(commands) >= 2:
            if commands[-2] == "DD 1" and commands[-1] == "PDATA 1":
                encoded_data.pop()
                assert jump_DD1_PDATA1 is not None
                encoded_data[-2] = jump_DD1_PDATA1 & 0xff
                commands.pop()
                commands.pop()
                commands.append('DD1_PDATA1')
            elif commands[-2] == "DD 1" and commands[-1] == "PDATA 2":
                encoded_data.pop()
                assert jump_DD1_PDATA2 is not None
                encoded_data[-2] = jump_DD1_PDATA2 & 0xff
                commands.pop()
                commands.pop()
                commands.append('DD1_PDATA2')
            elif commands[-2] == "DD 1" and commands[-1] == "PDATA 3":
                encoded_data.pop()
                assert jump_DD1_PDATA3 is not None
                encoded_data[-2] = jump_DD1_PDATA3 & 0xff
                commands.pop()
                commands.pop()
                commands.append('DD1_PDATA3')
            elif commands[-2] == "DD 2" and commands[-1] == "PDATA 1":
                encoded_data.pop()
                assert jump_DD2_PDATA1 is not None
                encoded_data[-3] = jump_DD2_PDATA1 & 0xff
                commands.pop()
                commands.pop()
                commands.append('DD2_PDATA1')
            elif commands[-2] == "DD 2" and commands[-1] == "PDATA 2":
                encoded_data.pop()
                assert jump_DD2_PDATA2 is not None
                encoded_data[-3] = jump_DD2_PDATA2 & 0xff
                commands.pop()
                commands.pop()
                commands.append('DD2_PDATA2')
            elif commands[-2] == "DD 1" and commands[-1] == "0x00 2":
                encoded_data.pop()
                assert jump_DD1_ZZ2 is not None
                encoded_data[-2] = jump_DD1_ZZ2 & 0xff
                commands.pop()
                commands.pop()
                commands.append('DD1_ZZ2')
            elif commands[-2] == "DD 1" and commands[-1] == "0xFF 2":
                encoded_data.pop()
                assert jump_DD1_FF2 is not None
                encoded_data[-2] = jump_DD1_FF2 & 0xff
                commands.pop()
                commands.pop()
                commands.append('DD1_FF2')
            elif commands[-2] == "0x00 8" and commands[-1] == "0x00 8":
                encoded_data.pop()
                encoded_data.pop()
                encoded_data.append(zz_code(16) & 0xff)
                commands.pop()
                commands.pop()
                commands.append("0x00 16")
            elif commands[-2] == "0x00 1" and commands[-1] == "DD 1":
                encoded_data[-3] = data_code(2) & 0xff
                encoded_data[-2] = 0
                commands.pop()
                commands.pop()
                commands.append("DD 2")
                continue
            elif commands[-2] == "0xFF 1" and commands[-1] == "DD 1":
                encoded_data[-3] = data_code(2) & 0xff
                encoded_data[-2] = 0xff
                commands.pop()
                commands.pop()
                commands.append("DD 2")
                continue
            elif commands[-2] == "0x00 1" and commands[-1] == "DD 2":   # 0x00 1 DD 2
                encoded_data[-4] = data_code(3) & 0xff
                encoded_data[-3] = 0
                commands.pop()
                commands.pop()
                commands.append("DD 3")
                continue
            elif commands[-2] == "0xFF 1" and commands[-1] == "DD 2":   # 0xFF 1 DD 2
                encoded_data[-4] = data_code(3) & 0xff
                encoded_data[-3] = 0xff
                commands.pop()
                commands.pop()
                commands.append("DD 3")
                continue
            elif commands[-2] == "DD 2" and commands[-1] == "PDATA 1":   # DD 2 PDATA 1
                encoded_data[-4] = data_code(3) & 0xff
                encoded_data[-1] = data[i - 1]
                commands.pop()
                commands.pop()
                commands.append("DD 3")
                continue
            elif commands[-2] == "DD 1" and commands[-1] == "0x00 2":   # DD 1 0x00 2
                # +speed, but size increases by one
                encoded_data.pop()
                encoded_data.append(0)
                encoded_data.append(0)
                encoded_data[-4] = data_code(3) & 0xff
                commands.pop()
                commands.pop()
                commands.append("DD 3")
                continue
            elif commands[-2] == "DD 1" and commands[-1] == "0xFF 2":   # DD 1 0xFF 2
                # +speed, but size increases by one
                encoded_data.pop()
                encoded_data.append(0xff)
                encoded_data.append(0xff)
                encoded_data[-4] = data_code(3) & 0xff
                commands.pop()
                commands.pop()
                commands.append("DD 3")
                continue
            elif commands[-2] == "PDATA 1" and commands[-1] == "DD 1":   # PDATA 1 DD 1
                # +speed, but size increases by one
                encoded_data[-3] = data_code(2) & 0xff
                encoded_data[-2] = data[i - 2]
                commands.pop()
                commands.pop()
                commands.append("DD 2")
                continue

        if pdata and data[i] == pdata[i]:
            count = 1
            while i + 1 < len(data) and data[i + 1] == pdata[i + 1] and count < MAX_SKIP_COUNT:
                count += 1
                i += 1
            control_byte = skip_code(count)
            encoded_data.append(control_byte & 0xff)
            if debug:
                print('PDATA', count)
            add_cmd(f"PDATA {count}")
        elif data[i] == 0xFF:
            start = i
            pdata_count = 0
            count = 1
            while i + 1 < len(data) and data[i + 1] == 0xFF and count < MAX_FF_COUNT:
                if pdata and data[i] == pdata[i]:
                    pdata_count += 1
                    if pdata_count >= pdata_break_count:
                        i -= pdata_count - 1
                        break
                else:
                    pdata_count = 0
                i += 1
                count += 1
            count = i - start + 1
            control_byte = ff_code(count)
            encoded_data.append(control_byte)
            if debug:
                print('0xFF', count, pdata[start:start+count] if pdata else None)
            add_cmd(f"0xFF {count}")
        elif data[i] == 0x00:
            count = 1
            start = i
            pdata_count = 0
            while i + 1 < len(data) and data[i + 1] == 0x00 and count < MAX_ZZ_COUNT:
                if pdata and data[i] == pdata[i]:
                    pdata_count += 1
                    if pdata_count >= pdata_break_count:
                        i -= pdata_count - 1
                        break
                else:
                    pdata_count = 0
                count += 1
                i += 1
            count = i - start + 1
            control_byte = zz_code(count)
            encoded_data.append(control_byte)
            if debug:
                print('0x00', count, pdata[start:start+count] if pdata else None)
            add_cmd(f"0x00 {count}")
        else:
            start = i
            last = None
            repeat_count = 0
            pdata_count = 0
            while i < len(data) and (i - start) < MAX_DATA_COUNT:
                if pdata and data[i] == pdata[i]:
                    pdata_count += 1
                    if pdata_count >= pdata_break_count:
                        i -= pdata_count - 1
                        break
                else:
                    pdata_count = 0
                if data[i] in (0x00, 0xFF):
                    # only break if the new data keeps on going
                    if i + 1 >= len(data) or data[i + 1] == data[i]:
                        break
                if MAX_REPEAT_COUNT > 0:
                    if data[i] == last:
                        repeat_count += 1
                    else:
                        if repeat_count >= MIN_REPEAT_COUNT:
                            break
                        repeat_count = 1
                        last = data[i]
                    if repeat_count == MAX_REPEAT_COUNT:  # Full
                        break
                i += 1

            # won't repeat less than 3
            if repeat_count < 3:
                repeat_count = 0

            count = i - start - repeat_count
            if count > 0:
                control_byte = data_code(count)
                encoded_data.append(control_byte)
                encoded_data.extend(data[start:start + count])
                if debug:
                    print('DD', count, data[start:start + count], pdata[start:start + count] if pdata else None)
                add_cmd(f"DD {count}")
                start = start + count

            if repeat_count > 0:
                control_byte = repeat_code(repeat_count)
                encoded_data.append(control_byte)
                encoded_data.append(data[start])
                if debug:
                    print('REPEAT', repeat_count, data[start])
                add_cmd(f"RR {repeat_count}")
            continue  # Avoid extra increment
        i += 1

    # Append end-of-data marker
    encoded_data.append(0)  # End of file marker

    with open(output_file, 'wb') as f:
        f.write(encoded_data)
    print(f"Encoded file saved to {output_file}")

def run_length_decode(input_file, output_file):
    assert False, "not implemented"

def generate_code():
    print(decoder_asm)

def debug():
    last_cmd = None
    for cmd in commands:
        print(f"CMD: {cmd}")
        if last_cmd is not None:
            print(f"LAST2: {last_cmd} {cmd}")
        last_cmd = cmd

def main():
    parser = argparse.ArgumentParser(description="Run-Length Encode or Decode a binary file.")
    parser.add_argument("--debug", action="store_true", help="Enable debug mode")

    subparsers = parser.add_subparsers(dest="mode", required=True)

    encode_parser = subparsers.add_parser("encode", help="Encode a file using run-length encoding")
    encode_parser.add_argument("input_file", help="Path to the input file")
    encode_parser.add_argument("output_file", help="Path to the output file")
    encode_parser.add_argument("--relative", nargs='?', type=str, help="Use relative frame to skip data")

    decode_parser = subparsers.add_parser("decode", help="Decode a run-length encoded file")
    decode_parser.add_argument("input_file", help="Path to the input file")
    decode_parser.add_argument("output_file", help="Path to the output file")

    gencode_parser = subparsers.add_parser("gencode", help="Generate encoding code")

    args = parser.parse_args()

    if args.mode == "encode":
        run_length_encode(args.input_file, args.output_file, args.relative, debug=args.debug)
    elif args.mode == "decode":
        run_length_decode(args.input_file, args.output_file)
    elif args.mode == "gencode":
        generate_code()

    if args.debug:
        debug()

if __name__ == "__main__":
    main()

