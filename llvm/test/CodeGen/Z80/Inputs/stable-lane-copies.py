"""Check copied constant lanes and page-changing word definitions."""
import argparse
import importlib.util
import random
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--llc', required=True)
parser.add_argument('--mir', type=Path, required=True)
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
args.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        assembly = subprocess.check_output([
            args.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(args.mir), '-o', '-'], text=True)
        assembly += '\nopaque:\n'
        if bank:
            assembly += '''exx
ld bc, 31415
ld de, 27182
ld hl, 16180
exx
ex af, af'
ld a, 173
ex af, af'
'''
        assembly += '''ld a, 231
ld bc, 12345
ld de, 23456
ld hl, 34567
ld iy, 45678
ret
'''
        (args.work / (allocator + str(bank) + '.s')).write_text(assembly)
        program = execution.Program(assembly)
        rng = random.Random(724)
        for name in ('copied_low', 'copied_high', 'carry_page', 'different_pages'):
            for value in range(256):
                cpu = execution.CPU(rng)
                cpu.put('e', value)
                cpu.put('d', (value + 1) & 255)
                before = bytes(cpu.mem)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                expected = bytearray(before)
                destinations = set()
                if name.startswith('copied_'):
                    high = name == 'copied_high'
                    addresses = [((((value-step)&255)<<8)|49 if high else
                                  0x3100|((value+step)&255)) for step in range(4)]
                    samples = [17, 34, 51, 68]
                elif name == 'carry_page':
                    word = 0x3100 + value + 1
                    addresses = [0x3100|value, word,
                                 (word & 0xff00)|((word+1)&255)]
                    samples = [17]*3
                else:
                    addresses = [0x3100|value, 0x4200|((value+1)&255),
                                 0x4200|((value+2)&255)]
                    samples = [17]*3
                for address, sample in zip(addresses, samples):
                    expected[address] = sample
                    destinations.add(address)
                result = addresses[-1]
                if name == 'copied_low':
                    result = (result & 0xff00) | ((result + 1) & 255)
                elif name == 'copied_high':
                    result = (result - 256) & 65535
                assert cpu.get('de') == result, (allocator, bank, name, value)
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.mem[:sp - 256] == expected[:sp - 256]
                assert cpu.mem[sp + 2:] == expected[sp + 2:]
                assert cpu.writes <= destinations | set(range(sp - 256, sp))
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} stable-lane-copy executions passed')
