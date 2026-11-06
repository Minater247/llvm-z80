"""Check consumed AF spills and live accumulator, flag and repeated-read values."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
checks = 0
for name in ('consume', 'flags_live', 'alternate_flags_live',
             'multiple_reads', 'primary_live'):
    for value in range(256):
        for flags in (0, 1, 64, 255):
            cpu = x.CPU(random.Random(value * 257 + flags))
            cpu.put('a', value)
            cpu.put('b', value ^ 0xa5)
            cpu.put('f', flags)
            cpu.alt['f'] = flags
            ix, sp = cpu.get('ix'), cpu.sp
            cpu.run(program, entry=name)
            assert cpu.get('a') == value, (name, value, flags)
            assert cpu.get('ix') == ix and cpu.sp == sp + 2
            if name == 'flags_live':
                assert bool(cpu.get('f') & 1) == (value < 128)
                assert bool(cpu.get('f') & 64) == (value == 128)
            if name == 'alternate_flags_live':
                assert cpu.alt['f'] == flags
            if name == 'multiple_reads':
                assert cpu.mem[16384] == value
            if name == 'primary_live':
                assert cpu.get('c') == value ^ 0xa5
            checks += 1
print(f'Checked {checks} AF spill executions')
