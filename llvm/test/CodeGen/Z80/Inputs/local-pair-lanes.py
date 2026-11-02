"""Check local cursor updates, byte wrapping and caller-saved clobbers."""
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
        for name in ('low_cursor', 'high_cursor', 'fixed_ports'):
            for value in range(256):
                cpu = execution.CPU(rng)
                cpu.put('e', value)
                cpu.put('d', (value + 1) & 255)
                before = bytes(cpu.mem)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                expected = bytearray(before)
                destinations = set()
                if name == 'low_cursor':
                    for step in range(4):
                        address = 0x3100 | ((value + step) & 255)
                        expected[address] = 17 * (step + 1)
                        destinations.add(address)
                    result = 0x3100 | ((value + 3) & 255)
                elif name == 'high_cursor':
                    result = (((value - 3) & 255) << 8) | 49
                    assert cpu.get('a') == before[result]
                    for step in range(4):
                        address = (((value - step) & 255) << 8) | 49
                        assert cpu.reads.count(address) == 1
                else:
                    for step in range(4):
                        address = 0x3100 | ((value + step % 2) & 255)
                        expected[address] = 17 * (step + 1)
                        destinations.add(address)
                if name != 'fixed_ports':
                    assert cpu.get('de') == result, (allocator, bank, name, value)
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.mem[:sp - 256] == expected[:sp - 256]
                assert cpu.mem[sp + 2:] == expected[sp + 2:]
                assert cpu.writes <= destinations | set(range(sp - 256, sp))
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} local-pair executions passed')
