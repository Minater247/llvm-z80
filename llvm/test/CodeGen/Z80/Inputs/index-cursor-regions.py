"""Check a cursor crossing calls and entering a cheaper loop register."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
assembly = Path(sys.argv[1]).read_text() + '''
address_scratch:
 ld hl, 4660
 xor a
 ret
'''
program = x.Program(assembly)
for seed in range(32):
    for base in (0, 0xffff, 0xfff0, 0x30fe):
        cpu = x.CPU(random.Random(seed))
        expected = cpu.mem[(base+31)&65535]
        cpu.put('de', base)
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry='cursor_regions')
        assert cpu.mem[16384] == expected, (seed, base)
        assert cpu.get('hl') == (base+32)&65535, (seed, base)
        assert cpu.sp == sp+2 and cpu.get('ix') == ix
print('128 index-cursor region executions passed')
