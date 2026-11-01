"""Execute emitted ADL bitwise code, including upper bytes and real stack slots.

The model implements the straight-line instruction subset used by these tests;
an unsupported instruction fails explicitly. Pair operations and stack entries
use 24 bits, while byte-register writes preserve the other two register lanes.
"""
import importlib.util
from pathlib import Path
import random
import re
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
spec = importlib.util.spec_from_file_location(
    'adl', Path(__file__).with_name('ez80-execution.py'))
adl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adl)
CPU, MASK = adl.CPU, adl.MASK

rng = random.Random(24083)
edges = [0, 1, 255, 256, 65535, 65536, 0x7fffff, 0x800000, MASK]
inputs = [(x, y) for x in edges for y in edges]
inputs += [(rng.randrange(1 << 24), rng.randrange(1 << 24)) for _ in range(1024)]
checks = 0
for x, y in inputs:
    for name, expected in [('and24', x & y), ('or24', x | y),
                           ('xor24', x ^ y), ('not24', x ^ MASK),
                           ('mask_pointer', x & y)]:
        cpu = CPU(rng)
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.store(sp, 0x654321, 3)
        cpu.store(sp + 3, x, 3)
        cpu.store(sp + 6, y, 3)
        cpu.writes.clear()
        cpu.run(program, name)
        assert cpu.get('hl') == expected, (name, x, y, cpu.get('hl'), expected)
        assert cpu.sp == sp + 3 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 128, sp))
        checks += 1
print(f'{checks} complete ADL bitwise executions passed')
