"""Execute delayed pair construction across calls and changing byte values."""
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
count = 0
for allocator in ('greedy', 'z80'):
    for bank in (False, True):
        assembly = subprocess.check_output([
            args.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(args.mir), '-o', '-'], text=True)
        # The callee destroys the caller-saved registers available in this
        # configuration. Its return value and the loaded bytes are irrelevant.
        assembly += '\nopaque:\n'
        if bank:
            assembly += """exx
ld bc, 31415
ld de, 27182
ld hl, 16180
exx
ex af, af'
ld a, 173
ex af, af'
"""
        assembly += '''
ld a, 231
ld bc, 12345
ld de, 23456
ld hl, 34567
ld iy, 45678
ret
'''
        (args.work / (allocator + str(bank) + '.s')).write_text(assembly)
        program = execution.Program(assembly)
        rng = random.Random(924)
        for value in range(256):
            for name in ('late_pair', 'dead_source', 'changed_source',
                         'loop_value', 'subregister_source'):
                cpu = execution.CPU(rng)
                cpu.put('e', value)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                expected = value * (8 if name == 'loop_value' else 2)
                assert cpu.get('de') == expected, (allocator, bank, name, value)
                if name != 'dead_source':
                    assert cpu.get('a') == (7 if name == 'changed_source' else value)
                if name == 'subregister_source':
                    assert cpu.get('bc') == 9
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                if not bank:
                    assert cpu.alt == alternate
                count += 1
print(f'{count} pair-construction executions passed')
