"""Check word shifts across byte boundaries, sign bits and live operands."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(521)
values = {v for v in range(256)} | {v << 8 for v in range(256)}
values |= {0x7fff, 0x8000, 0x8001, 0xfffe, 0xffff}
values |= {rng.randrange(65536) for _ in range(512)}
checks = 0

def run(name, x, y, expected):
    global checks
    cpu = execution.CPU(rng)
    cpu.put('de', x)
    cpu.put('bc', y)
    if name == 'narrow_load':
        cpu.mem[0x6000] = x
        cpu.put('de', 0x6000)
    sp, ix = cpu.sp, cpu.get('ix')
    cpu.run(program, entry=name)
    assert cpu.get('hl') == expected & 65535, (name, x, y, cpu.get('hl'))
    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
    assert cpu.writes <= set(range(sp - 256, sp))
    if name == 'narrow_load':
        assert cpu.reads.count(0x6000) == 1
    checks += 1

for x in sorted(values):
    signed = x if x < 32768 else x - 65536
    for n in [1, 2, 3, 4, 5, 7, 8, 15]:
        run('lshr' + str(n), x, 0, x >> n)
        run('ashr' + str(n), x, 0, signed >> n)
    y = rng.randrange(65536)
    signed_y = y if y < 32768 else y - 65536
    run('live_shifts', x, y, ((x >> 3) + (signed_y >> 4)) ^ x)
    if x < 256:
        run('narrow_input', x, 0, x >> 4)
        run('narrow_load', x, 0, x >> 4)
print(f'{checks} word-shift executions passed')
