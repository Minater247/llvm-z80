"""Check a bank region's memory effects and preservation of primary pairs."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
for start in range(256):
    cpu = x.CPU(random.Random(start))
    cpu.alt['h'], cpu.alt['l'] = 0x20, 0
    cpu.alt['d'], cpu.alt['e'] = 0x20, 1
    before = {r: cpu.get(r) for r in ('bc', 'de', 'hl', 'ix')}
    cpu.mem[0x2000] = 1
    cpu.mem[0xc000:0xc002] = bytes((start, 0xa0))
    sample = cpu.mem[0xa000+start]
    cpu.run(program, entry='region')
    assert cpu.mem[0x2000:0x2002] == bytes((7, sample))
    assert cpu.mem[0xc000:0xc002] == bytes(((start+1) & 255, 0xa0))
    assert cpu.alt['h'] * 256 + cpu.alt['l'] == 8192
    assert cpu.alt['d'] * 256 + cpu.alt['e'] == 8193
    assert all(cpu.get(r) == v for r, v in before.items())
    assert cpu.sp == 0xf002
print('256 joint bank-region executions passed')
