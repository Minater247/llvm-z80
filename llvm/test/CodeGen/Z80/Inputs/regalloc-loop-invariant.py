"""Execute loop-local invariants, shared exits and loop-updated values."""
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


class CPU(execution.CPU):
    def __init__(self, rng):
        super().__init__(rng)
        self.values = []

    def write(self, operand, value, width=1):
        if operand == '(12544)':
            self.values.append(value & 255)
        super().write(operand, value, width)


checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        assembly = subprocess.check_output([
            args.llc, '-start-before=' + ('z80-regalloc' if allocator == 'z80'
                                         else 'greedy'),
            '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(args.mir), '-o', '-'], text=True)
        assembly += '\npreserving:\nret\nopaque:\n'
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
        rng = random.Random(421)
        for name in ('invariant', 'conditional_loop', 'updated'):
            for value in range(256):
                cpu = CPU(rng)
                cpu.put('e', value)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                count = 0 if name == 'conditional_loop' and value & 1 else 8
                updated = name == 'updated'
                expected = [(value + i * updated) & 255 for i in range(count)]
                assert cpu.values == expected, (allocator, bank, name, value)
                assert cpu.get('a') == (value + count * updated) & 255
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.writes <= {12544} | set(range(sp - 256, sp))
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} loop-invariant executions passed')
