"""Check constant word shifts and preservation of simultaneously live operands."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(1615)
values = set(range(256)) | {v << 8 for v in range(256)}
values |= {0x7fff, 0x8000, 0x8001, 0xfffe, 0xffff}
values |= {rng.randrange(65536) for _ in range(512)}
checks = 0
for x in sorted(values):
    y = rng.randrange(65536)
    for n in range(17):
        cpu = execution.CPU(rng)
        cpu.put('de', x)
        cpu.put('bc', y)
        sp, ix = cpu.sp, cpu.get('ix')
        name = 'shl' + str(n) if n < 16 else 'live_shift'
        expected = x << n if n < 16 else ((x << 15) + y) ^ x
        cpu.run(program, entry=name)
        assert cpu.get('hl') == expected & 65535, (name, x, y, cpu.get('hl'))
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 256, sp))
        checks += 1
print(f'{checks} constant word left-shift executions passed')
