"""Check byte increment results and overflow branches for every input byte."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(103)
checks = 0
for x in range(256):
    for y in [0, 1, 127, 128, 255]:
        for name, step in [('wrap', 1), ('commuted', 1), ('step_two', 2)]:
            cpu = execution.CPU(rng)
            cpu.put('e', x)
            cpu.put('d', y)
            sp, ix = cpu.sp, cpu.get('ix')
            cpu.run(program, entry=name)
            assert cpu.get('a') == ((x + step) & 255) ^ y, (name, x, y)
            assert cpu.mem[0x6000] == (y if x + step > 255 else x)
            assert cpu.sp == sp + 2 and cpu.get('ix') == ix
            assert cpu.writes <= set(range(sp - 256, sp)) | {0x6000}
            checks += 1
print(f'{checks} complete byte-overflow branch executions passed')
