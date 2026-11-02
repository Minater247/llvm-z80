"""Check rematerialized constants and updated pointers across clobbering calls."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
assembly = Path(sys.argv[1]).read_text() + '''
clobber:
ld bc, 17185
ld de, 30292
ld hl, 22567
ld iy, 15432
ld a, 0
ret
'''
program = execution.Program(assembly)
rng = random.Random(17)
for value in range(256):
    for name, address in (('constant_copy', 4660), ('updated_pointer', 4661)):
        cpu = execution.CPU(rng)
        cpu.mem[4660] = value ^ 255
        cpu.mem[address] = value
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry=name)
        assert cpu.get('a') == value, (name, value)
        assert cpu.reads.count(address) == 1
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 256, sp))
print('512 bank constant executions passed')
