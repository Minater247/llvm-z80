"""Verify linked callback and startup pointers across native and LTO builds."""
from pathlib import Path
import subprocess
import sys

clang, work = sys.argv[1], Path(sys.argv[2])


def run(args):
    return subprocess.check_output([str(arg) for arg in args], text=True)


def verify(elf, output, triple, mode, width, base, callback_base):
    symbols = {parts[2]: int(parts[0], 16)
               for line in run(['llvm-nm', '-n', elf]).splitlines()
               if len(parts := line.split()) == 3}
    assert 'unused' not in symbols, (triple, mode, 'section GC')
    assert symbols['invoke'] == base, (triple, mode, 'entry placement')
    assert symbols['callbacks'] == callback_base, (triple, mode, 'table placement')
    assert symbols['shared'] >= callback_base + 0x100, (triple, mode, 'state placement')
    for section, names in [('.callbacks', ['increment', 'flip']),
                           ('.init_array', ['initialize'])]:
        data = output / (section[1:] + '.bin')
        run(['llvm-objcopy', '--dump-section', section + '=' + str(data), elf])
        expected = b''
        for name in names:
            addresses = [address for symbol, address in symbols.items()
                         if symbol == name or symbol.startswith(name + '.llvm.')]
            assert len(addresses) == 1, (triple, mode, name, addresses)
            expected += addresses[0].to_bytes(width, 'little')
        assert data.read_bytes() == expected, (triple, mode, section)
    assert symbols['__init_array_end'] - symbols['__init_array_start'] == width
    print(triple, mode, elf.name, 'callback and constructor pointers verified')


for triple, width, base in [
    ('z80', 2, 0x100),
    ('ez80-unknown-none-code16', 2, 0x100),
    ('ez80', 3, 0x123400),
]:
    callback_base = 0x700 if width == 2 else 0x234500
    for mode, lto in [('plain', []), ('full', ['-flto']),
                      ('thin', ['-flto=thin'])]:
        output = work / (triple + '-' + mode)
        output.mkdir(exist_ok=True)
        common = [clang, '--target=' + triple, '-O2', '-ffreestanding',
                  '-ffunction-sections', '-fdata-sections', *lto]
        obj, archive = output / 'worker.o', output / 'worker.a'
        run([*common, '-c', work / 'worker.c', '-o', obj])
        archive.unlink(missing_ok=True)
        run(['llvm-ar', 'rcs', archive, obj])
        elf = output / 'image.elf'
        run([*common, '-nostdlib', '-Wl,--gc-sections',
             '-Wl,--defsym=code_base=' + str(base),
             '-Wl,--defsym=callback_base=' + str(callback_base),
             '-T', work / 'link.ld', work / 'entry.c', work / 'runtime.S',
             archive, '-o', elf])

        # A relocatable LTO link must preserve unresolved runtime references and
        # callback relocations for the final firmware link, including ADL's
        # three-byte pointers. Native objects follow the same two-stage path.
        partial = output / 'partial.o'
        run([*common, '-nostdlib', '-r', work / 'entry.c', archive,
             '-o', partial])
        relocations = run(['llvm-readobj', '-h', '-r', partial])
        assert 'Type: Relocatable' in relocations, (triple, mode)
        assert 'R_Z80_' + str(width * 8) in relocations, (triple, mode)
        assert 'startup_value' in relocations, (triple, mode)
        staged = output / 'staged.elf'
        run([*common, '-nostdlib', '-Wl,--gc-sections',
             '-Wl,--defsym=code_base=' + str(base),
             '-Wl,--defsym=callback_base=' + str(callback_base),
             '-T', work / 'link.ld', work / 'runtime.S', partial,
             '-o', staged])
        for linked in [elf, staged]:
            verify(linked, output, triple, mode, width, base, callback_base)
