"""Check word spills with dead scratch pairs, live halves and overlapping bases."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(619)
checks = 0
for name in ('spill_index', 'spill_dead_scratch', 'load_dead_scratch',
             'load_live_high', 'load_overlap'):
    for low in range(256):
        cpu = execution.CPU(rng)
        value = low | (rng.randrange(256) << 8)
        cpu.put('ix', 0x2002)
        cpu.put('iy', value)
        if name == 'load_overlap':
            cpu.put('hl', 0x2000)
        cpu.mem[0x2000], cpu.mem[0x2001] = value & 255, value >> 8
        saved = {r: cpu.get(r) for r in ('hl', 'h', 'ix', 'iy')}
        sp = cpu.sp
        cpu.run(program, entry=name)
        if name.startswith('spill'):
            assert cpu.mem[0x2000] | cpu.mem[0x2001] << 8 == value
            assert cpu.get('iy') == saved['iy']
        else:
            assert cpu.get('hl' if name == 'load_overlap' else 'iy') == value
        if name == 'spill_index': assert cpu.get('hl') == saved['hl']
        if name == 'load_live_high': assert cpu.get('h') == saved['h']
        assert cpu.get('ix') == saved['ix'] and cpu.sp == sp + 2
        checks += 1
print(f'{checks} index spill scratch executions passed')
