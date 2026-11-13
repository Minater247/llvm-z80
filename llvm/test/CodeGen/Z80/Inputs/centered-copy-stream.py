"""Check centered byte streams with overlapping and disjoint memory."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
rng = random.Random(8708)
checks = 0
for name, offsets in [('ascending', range(-7, 9)),
                      ('descending', range(8, -8, -1))]:
    for source in (7, 0xff, 0x100, 0x207, 0x7fff, 0xfff7):
        for distance in (-19, -1, 0, 1, 19, 0x2000):
            cpu = x.CPU(rng)
            dest = (source + distance) & 65535
            expected = bytearray(cpu.mem)
            touched = set()
            for offset in offsets:
                address = (dest + offset) & 65535
                expected[address] = expected[(source + offset) & 65535]
                touched.add(address)
            cpu.put('de', source)
            cpu.put('bc', dest)
            sp = cpu.sp
            cpu.run(program, entry=name)
            assert all(cpu.mem[a] == expected[a] for a in touched), (name, source, dest)
            assert cpu.sp == sp + 2
            assert cpu.writes <= touched | set(range(sp - 256, sp))
            checks += 1
print('Checked', checks, 'centered copy streams')
