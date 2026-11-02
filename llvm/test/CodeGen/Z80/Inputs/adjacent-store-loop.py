"""Check pointer restoration on backedges and accumulator-live rejection."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
for count in range(1, 17):
    for value in range(256):
        cpu = x.CPU(random.Random(value))
        cpu.put('a', count)
        cpu.mem[16384] = value
        neighbor = cpu.mem[16386]
        cpu.run(program, entry='loop_stores')
        assert cpu.get('a') == (value if count == 1 else 7)
        assert cpu.mem[16384] == 7 and cpu.mem[16385] == 1
        assert cpu.mem[16386] == neighbor and cpu.sp == 0xf002
for value in range(256):
    cpu = x.CPU(random.Random(value))
    cpu.put('e', value)
    cpu.run(program, entry='live_accumulator')
    assert cpu.get('de') == 16384 and cpu.get('b') == value
    assert cpu.mem[16384] == 7 and cpu.mem[16385] == value
    assert cpu.sp == 0xf002
print('4352 adjacent-store executions passed')
