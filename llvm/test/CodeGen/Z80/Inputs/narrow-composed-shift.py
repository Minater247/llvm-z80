"""Execute narrowed shifts and cases that retain the high lane."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
rng = random.Random(803)
checks = 0
for value in range(256):
    for other in (0, 1, 7, 8, 127, 128, 255, rng.randrange(256)):
        for name, expected in (
                ('composed', ((value & 1) << 3) | other | 256),
                ('high_lane', (value << 3) | other | 256),
                ('byte_boundary', (value << 8) | other)):
            cpu = x.CPU(rng)
            cpu.put('e', value)
            cpu.put('d', other)
            sp = cpu.sp
            cpu.run(program, entry=name)
            assert cpu.get('hl') == expected, (name, value, other)
            assert cpu.sp == sp + 2
            checks += 1
    for count in range(8):
        cpu = x.CPU(rng)
        cpu.put('de', value | (rng.randrange(256) << 8))
        cpu.put('c', count)
        sp = cpu.sp
        cpu.run(program, entry='variable_low')
        assert cpu.get('a') == (value << count) & 255, (value, count)
        assert cpu.sp == sp + 2
        checks += 1
print('Checked', checks, 'composed shifts')
