"""Check composed addresses, byte lookups and offsets that carry across pages."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(580)
checks = 0

def run(name, x, y, expected, byte_args=False):
    global checks
    cpu = execution.CPU(rng)
    cpu.put('e' if byte_args else 'de', x)
    cpu.put('d' if byte_args else 'bc', y)
    sp, ix = cpu.sp, cpu.get('ix')
    if name.startswith('lookup'):
        address = x * 256 + y
        cpu.mem[address] = expected
    cpu.run(program, entry=name)
    result = cpu.get('a' if name.startswith('lookup') else 'hl')
    assert result == expected & 65535, (name, x, y, result, expected)
    if name.startswith('lookup'):
        assert cpu.reads.count(address) == 1
    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
    assert cpu.writes <= set(range(sp - 256, sp))
    checks += 1

for page in range(256):
    for index in [0, 1, 127, 128, 248, 249, 254, 255]:
        run('page_address', page, index, page * 256 + index, True)
        run('reversed_address', page, index, page * 256 + index, True)
        signed = index if index < 128 else index - 256
        run('signed_offset', page, index, page * 256 + signed, True)
        run('reversed_signed', page, index, page * 256 + signed, True)
        base = page * 256 + rng.randrange(256)
        run('masked_address', base, index, page * 256 + index)
        run('unaligned', base, index, page * 256 + 7 + index)
        run('reversed_unaligned', base, index, page * 256 + 7 + index)
        run('constant_page', base, index, 16384 + index)
    run('constant_offset', page * 256 + 255, 0, page * 256 + 1)
for _ in range(1024):
    base, index = rng.randrange(65536), rng.randrange(65536)
    run('wide_offset', base, index, (base & 0xff00) + index)
    run('masked_address', base, index, (base & 0xff00) | (index & 255))
for index in range(256):
    run('lookup', 0x40 + (index & 15), index, rng.randrange(256), True)
    run('lookup_reversed', 0x40 + (index & 15), index, rng.randrange(256), True)
print(f'{checks} page-address executions passed')
