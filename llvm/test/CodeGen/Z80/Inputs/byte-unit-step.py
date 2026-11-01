"""Check unit byte arithmetic and zero tests for every input byte."""
import importlib.util
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(725)
checks = 0
for value in range(256):
    for name, result in (
            ('increment', value + 1), ('decrement', value - 1),
            ('commuted', value + 1), ('subtract_one', value - 1),
            ('subtract_negative', value + 1), ('increment_zero', value == 255)):
        cpu = execution.CPU(rng)
        cpu.put('e', value)
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry=name)
        assert cpu.get('a') == result & 255, (name, value, cpu.get('a'))
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 256, sp))
        checks += 1
print(f'{checks} unit-byte executions passed')
