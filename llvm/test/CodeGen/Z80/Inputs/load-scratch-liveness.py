"""Check scratch preservation across split word loads and bank optimization."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(1984)
count = 0
for _ in range(256):
    for name in ('address', 'overlap', 'low', 'high', 'dead'):
        cpu = execution.CPU(rng)
        cpu.put('ix', rng.randrange(0x2000, 0x3000))
        cpu.put('hl', rng.randrange(0x4000, 0x5000))
        pointer = rng.randrange(0x6000, 0x7000)
        cpu.alt['h'], cpu.alt['l'] = pointer >> 8, pointer & 255
        sp, ix, hl, de, flags = (cpu.sp, cpu.get('ix'), cpu.get('hl'),
                                cpu.get('de'), cpu.get('f'))
        source = hl if name == 'overlap' else ix
        word = cpu.mem[source] | cpu.mem[source + 1] << 8
        sample = cpu.mem[pointer]
        cpu.run(program, entry=name)
        if name == 'overlap':
            assert cpu.get('hl') == word and cpu.get('de') == de
        else:
            assert cpu.get('iy') == word
        if name == 'address':
            assert cpu.get('hl') == pointer and cpu.get('e') == sample
            assert cpu.mem[pointer] == sample
        if name == 'low':
            assert cpu.get('l') == hl & 255
        if name == 'high':
            assert cpu.get('h') == hl >> 8
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        assert cpu.get('f') == flags
        assert cpu.writes <= set(range(sp - 8, sp)) | ({pointer} if name == 'address' else set())
        count += 1
print(f'{count} scratch-load executions passed')
