"""Check loop-carried values across a conditionally executed clobbering call."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
assembly = Path(sys.argv[1]).read_text()
program = x.Program(assembly + '''
clobber:
ld bc, 4660
ld de, 22136
ld hl, 39612
xor a
ret
''')
checks = 0
for initial in range(256):
    for count in (0, 1, 2, 7, 16, 255):
        cpu = x.CPU(random.Random(initial * 256 + count))
        cpu.put('e', initial)
        cpu.mem[8192], cpu.mem[8193] = count, 97
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry='dispatch')
        expected = (initial + 2 * count + count // 2) & 255
        assert cpu.get('a') == expected, (initial, count, cpu.get('a'), expected)
        assert cpu.mem[8192] == 0
        assert cpu.mem[8193] == (expected if count else 97)
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        checks += 1
print(f'Checked {checks} conditional-call executions')
