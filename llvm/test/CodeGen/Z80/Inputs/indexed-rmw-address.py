"""Check conditional indexed updates and pointer preservation across a call."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(23127)
for value in range(256):
    for condition in (0, 1, 255):
        cpu = execution.CPU(rng)
        base = 0x2000
        cpu.put('de', base)
        cpu.put('c', condition)
        for offset in (23, -128, 127): cpu.mem[base+offset] = value
        sp = cpu.sp
        cpu.run(program, entry='caller')
        assert cpu.get('hl') == base
        assert cpu.mem[base+23] == ((value+1)&255 if condition else value)
        assert cpu.mem[base-128] == ((value-1)&255 if condition else value)
        assert cpu.mem[base+127] == (value|32 if condition else value)
        assert cpu.sp == sp+2
print('768 indexed read-modify-write executions passed')
