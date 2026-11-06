"""Check accumulator regions through comparisons, joins and clobbering calls."""
import argparse
import importlib.util
from pathlib import Path
import random
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--llc', required=True)
p.add_argument('--mir', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
args = p.parse_args()
args.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        asm = subprocess.check_output([
            args.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(args.mir), '-o', '-'], text=True)
        asm += '\nopaque:\n'
        if bank:
            asm += '''exx
ld bc, 31415
ld de, 27182
ld hl, 16180
exx
ex af, af'
ld a, 173
ex af, af'
'''
        asm += '''ld a, 231
ld bc, 12345
ld de, 23456
ld hl, 34567
ld iy, 45678
ret
'''
        (args.work / f'{allocator}-{bank}.s').write_text(asm)
        program = x.Program(asm)
        rng = random.Random(912)
        for value in range(256):
            expected = {
                'classify': (255 if value < 2 else value if value < 32 else
                             (value + ((value << 1 | value >> 7) & 255)) & 255),
                'diamond': value,
                'replacement': 173 if value >= 32 else 99 if value >= 16 else value,
            }
            for name, result in expected.items():
                cpu = x.CPU(rng)
                cpu.put('e', value)
                sp, ix, alt = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                assert cpu.get('a') == result, (allocator, bank, name, value)
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                if not bank:
                    assert cpu.alt == alt
                checks += 1
print(f'{checks} accumulator-branch executions passed')
