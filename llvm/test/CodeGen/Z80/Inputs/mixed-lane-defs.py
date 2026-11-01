"""Check byte replacements around word definitions, including carry and wrap."""
import argparse
import importlib.util
import random
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--llc', required=True)
p.add_argument('--mir', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
rng = random.Random(812)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        asm = subprocess.check_output([
            a.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator,
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            '-verify-machineinstrs', a.mir, '-o', '-'], text=True)
        (a.work / (allocator + str(bank) + '.s')).write_text(asm)
        program = execution.Program(asm)
        for high in (0, 18, 127, 255):
            for low in range(256):
                value = high * 256 + low
                added = (value + 15) & 65535
                for name, word, result in (
                        ('carry_between_byte_defs', added, (added & 65280) | 7),
                        ('replace_word_between_byte_defs', value, 0x3407)):
                    cpu = execution.CPU(rng)
                    cpu.put('de', value)
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry=name)
                    assert (cpu.get('de'), cpu.get('hl')) == (word, result), (
                        allocator, bank, name, value, cpu.get('de'), cpu.get('hl'))
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    assert cpu.writes <= set(range(sp - 256, sp))
                    checks += 1
print(f'{checks} mixed-lane executions passed')
