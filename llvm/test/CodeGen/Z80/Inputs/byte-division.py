"""Check byte division, shared remainders and values live through new loops."""
import importlib.util
from pathlib import Path
import random
import sys

DIVISORS = [1, 2, 4] + list(range(8, 256))
SINGLES = [1, 2, 4, 8, 9, 12, 16, 17, 31, 32, 64, 127, 128, 129, 255]

if sys.argv[1] == '--emit':
    text = Path(sys.argv[2]).read_text()
    for d in DIVISORS:
        attr = 'minsize' if d & 1 else ''
        text += f'''
define i16 @pair_{d}(i8 %x) nounwind {attr} {{
  %q = udiv i8 %x, {d}
  %p = mul i8 %q, {d}
  %r = sub i8 %x, %p
  %qw = zext i8 %q to i16
  %rw = zext i8 %r to i16
  %high = shl i16 %rw, 8
  %result = or i16 %qw, %high
  ret i16 %result
}}
'''
    for d in SINGLES:
        for op in ['udiv', 'urem']:
            text += f'''
define i8 @{op}_{d}(i8 %x) nounwind minsize {{
  %r = {op} i8 %x, {d}
  ret i8 %r
}}
'''
        text += f'''
define i16 @explicit_{d}(i8 %x) nounwind minsize {{
  %q = udiv i8 %x, {d}
  %r = urem i8 %x, {d}
  %qw = zext i8 %q to i16
  %rw = zext i8 %r to i16
  %high = shl i16 %rw, 8
  %result = or i16 %qw, %high
  ret i16 %result
}}
'''
    Path(sys.argv[3]).write_text(text)
    sys.exit(0)

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(8012)
cpu = execution.CPU(rng)
checks = 0


def run(name, x, expected, word=False, y=0):
    global checks
    for bank in [cpu.r, cpu.alt]:
        for reg in bank:
            bank[reg] = rng.randrange(256)
    cpu.sp = 0xf000
    cpu.cycles = 0
    cpu.writes.clear()
    cpu.reads.clear()
    cpu.put('e', x)
    cpu.put('d', y)
    ix = cpu.get('ix')
    cpu.run(program, entry=name, max_steps=1000)
    actual = cpu.get('hl' if word else 'a')
    assert actual == expected & (65535 if word else 255), (name, x, y, actual, expected)
    assert cpu.sp == 0xf002 and cpu.get('ix') == ix, name
    assert cpu.calls == 0, name
    assert cpu.writes <= set(range(0xef00, 0xf000)), name
    checks += 1


for d in DIVISORS:
    for x in range(256):
        run(f'pair_{d}', x, x // d + (x % d) * 256, True)
for d in SINGLES:
    for x in range(256):
        run(f'udiv_{d}', x, x // d)
        run(f'urem_{d}', x, x % d)
        run(f'explicit_{d}', x, x // d + (x % d) * 256, True)
for x in range(256):
    run('reconstruct', x, x // 12 + (x % 12) * 256, True)
    run('different_factor', x, x - (x // 12) * 14)
    for y in [0, 11, 12, 17, 127, 128, 255]:
        run('different_source', x, x - (y // 12) * 12, y=y)
        run('conditional', x, x + (x // 12 if x < 128 else y % 17), y=y)
print(f'{checks} byte-division executions passed')
