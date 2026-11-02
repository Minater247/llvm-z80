"""Check low-byte recurrences, carry propagation, and observable upper bytes."""
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
for low in range(256):
    for a, b in ((low, 0), (0x8100 | low, 0x7fff), (0xffff, low),
                 (low, 0xff00 | (low ^ 255))):
        u, v = a, b
        for _ in range(7):
            u, v = ((u + v) ^ 42293) & 65535, (u - v) & 65535
        for name, expected, result in (
                ('byte_quotient', ((a - b) & 255) // 17, 'a'),
                ('coupled_bytes', (u | v) & 255, 'a'),
                ('high_observation', (u | v) >> 8, 'a'),
                ('full_result', (a - b) & 0xff00, 'hl')):
            cpu.sp = 0xf000
            cpu.put('de', a)
            cpu.put('bc', b)
            cpu.run(program, entry=name)
            assert cpu.get(result) == expected, (name, a, b, cpu.get(result), expected)
            assert cpu.sp == 0xf002
            checks += 1
print('Checked', checks, 'byte-only and full-width recurrences')
