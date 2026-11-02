"""Check copied cursor lanes, wraparound and overlapping live values."""
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
        for name in ('low_copies', 'high_copies', 'escaping_byte', 'observed_pair',
                     'call_boundary', 'call_groups'):
            for value in range(256):
                cpu = execution.CPU(rng)
                cpu.put('e', value)
                cpu.put('d', 49 if name == 'call_groups' else (value + 1) & 255)
                before = bytes(cpu.mem)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                expected = bytearray(before)
                destinations = set()
                high = name == 'high_copies'
                steps = range(6) if name == 'call_groups' else range(1, 4)
                for step in steps:
                    lane = (value - step if high else value + step) & 255
                    address = (lane << 8) | 49 if high else 0x3100 | lane
                    expected[address] = 34 if name == 'observed_pair' else 17
                    destinations.add(address)
                result = ((((value - 3) & 255) << 8) | 49 if high
                          else 0x3100 | ((value + steps[-1]) & 255))
                assert cpu.get('de') == result, (allocator, bank, name, value)
                if name in ('escaping_byte', 'call_groups'):
                    assert cpu.get('b') == (value + steps[-1]) & 255
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.mem[:sp - 256] == expected[:sp - 256]
                assert cpu.mem[sp + 2:] == expected[sp + 2:]
                assert cpu.writes <= destinations | set(range(sp - 256, sp))
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} copied-cursor executions passed')
