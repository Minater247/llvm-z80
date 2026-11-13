"""Verify distant alternate-accumulator slots preserve primary live registers."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
for value in range(256):
    for flags in (0, 1, 64, 255):
        cpu = x.CPU(random.Random(value * 257 + flags))
        cpu.put('a', value)
        cpu.put('f', flags)
        cpu.alt['a'] = value ^ 0xa5
        cpu.alt['f'] = flags
        primary = {reg: cpu.get(reg) for reg in ('a', 'bc', 'de', 'hl', 'ix')}
        sp = cpu.sp
        cpu.run(program, entry='far_slot')
        assert {reg: cpu.get(reg) for reg in primary} == primary, (value, flags)
        assert bool(cpu.get('f') & 1) == (value < 128)
        assert bool(cpu.get('f') & 64) == (value == 128)
        assert cpu.alt['a'] == value ^ 0xa5
        assert cpu.alt['f'] == flags
        assert cpu.sp == sp + 2
print('Checked 1024 distant AF spill executions')
