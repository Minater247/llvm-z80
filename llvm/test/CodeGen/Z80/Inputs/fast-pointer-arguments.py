"""Execute fast pointer calls with live integer arguments and local frames."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(416)
for name in ('large', 'small', 'caller'):
    for v in range(256):
        cpu = execution.CPU(rng)
        x, y, z = (17, 33, 32767) if name == 'caller' else (
            rng.randrange(65536), rng.randrange(65536), rng.randrange(65536))
        cpu.put('iy', 0x6000)
        cpu.put('de', x)
        cpu.put('bc', y)
        cpu.put('hl', z)
        cpu.mem[0x6000] = v
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry=name)
        assert cpu.get('hl') == (x + y + z + v) & 65535, (name, x, y, z, v)
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 256, sp))
print('768 fast pointer calls passed')
