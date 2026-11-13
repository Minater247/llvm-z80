"""Check shared index addresses across calls that destroy both register banks."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text() + """
opaque:
ld bc, 0
ld de, 0
ld hl, 0
ld iy, 0
exx
ld bc, 0
ld de, 0
ld hl, 0
exx
ex af, af'
xor a
ex af, af'
xor a
ret
""")
for value in range(256):
    cpu = x.CPU(random.Random(value))
    cpu.put('d', value)
    ix, sp = cpu.get('ix'), cpu.sp
    cpu.run(program, entry='indexed_writes')
    address = 0x3000 + value
    assert [cpu.mem[address + i] for i in range(3)] == [17, 34, 51], value
    assert cpu.get('de') == address, value
    assert cpu.get('ix') == ix and cpu.sp == sp + 2, value
print('Checked 256 indexed-address executions across clobbering calls')
