"""Check sequential byte constants and a partial word overwrite."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
for value in range(256):
    cpu = x.CPU(random.Random(value))
    cpu.mem[8194] = value
    before = {r: cpu.get(r) for r in ('bc', 'de', 'hl', 'ix')}
    cpu.run(program, entry='byte_sequence')
    assert cpu.mem[8192] == 42 and cpu.mem[8193] == value
    assert all(cpu.get(r) == v for r, v in before.items())
    assert cpu.sp == 0xf002
    cpu = x.CPU(random.Random(value))
    cpu.run(program, entry='partial_definition')
    assert cpu.get('hl') == 0x1256 and cpu.sp == 0xf002
print('512 local-constant executions passed')
