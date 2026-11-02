"""Check masked word comparisons and the retained high-byte dependency."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
cpu = x.CPU(random.Random(247))
checks = 0
for left in range(256):
    for right in (0, 1, 127, 128, 254, 255, left, left ^ 255):
        for name in ('masked_unsigned', 'masked_signed', 'masked_equal', 'unknown_high'):
            for high in (0, 0x8100):
                a = left | high
                cpu.sp = 0xf000
                cpu.put('de', a)
                cpu.put('bc', right | 0x7f00)
                cpu.run(program, entry=name)
                condition = ((left & 252) == right if name == 'masked_equal' else
                             (a if name == 'unknown_high' else left) < right)
                assert cpu.get('a') == (31 if condition else 79), (name, a, right)
                assert cpu.sp == 0xf002
                checks += 1
print('Checked', checks, 'word comparisons')
