"""Check conditional bank reloads, preserved sources and loop entry edges."""
import importlib.util
from pathlib import Path
import random
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)
assembly = subprocess.check_output([
    llc, '-mtriple=z80', '-mattr=+alternate-regs',
    '-start-before=z80-post-rewrite', '-verify-machineinstrs',
    source, '-o', '-'], text=True)
(directory/'result.s').write_text(assembly)
program = x.Program(assembly)
checks = 0
for entry in ('run', 'source_changed', 'used_here', 'both_paths'):
    for seed in range(256):
        for choice in (0, 1):
            cpu = x.CPU(random.Random(seed))
            pointer = 8192 + seed
            cpu.alt['d'], cpu.alt['e'] = pointer >> 8, pointer & 255
            cpu.put('a', choice)
            cpu.put('b', 3)
            expected = cpu.mem[pointer + (choice if entry == 'both_paths' else 0)]
            taken = pointer & 255 if entry == 'used_here' else choice
            sp, ix = cpu.sp, cpu.get('ix')
            cpu.run(program, entry=entry)
            result = expected if entry == 'both_paths' else 77 if taken else expected
            assert cpu.get('a') == result, (entry, seed, choice)
            assert cpu.sp == sp+2 and cpu.get('ix') == ix
            checks += 1
print(f'{checks} bank-edge executions passed')
