"""Check bank fan-out copies preserve both destinations, flags and other pairs."""
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
for entry, bank, pair, index in (('to_hl', 'de', 'hl', 'iy'),
                                ('to_bc', 'bc', 'bc', 'iy'),
                                ('to_de', 'hl', 'de', 'ix')):
    for seed in range(256):
        cpu = x.CPU(random.Random(seed))
        value = cpu.alt[bank[0]] * 256 + cpu.alt[bank[1]]
        before, alternate, sp = dict(cpu.r), dict(cpu.alt), cpu.sp
        cpu.put(pair, value)
        cpu.put(index, value)
        expected = dict(cpu.r)
        cpu.r = before
        cpu.run(program, entry=entry)
        assert cpu.r == expected, (entry, seed, cpu.r, expected)
        assert cpu.alt == alternate and cpu.sp == sp + 2
        checks += 1
print(f'{checks} bank-copy executions passed')
