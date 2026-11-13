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
             'multiple_reads', 'primary_live', 'competing_slots',
             'alternate_store_flags', 'mixed_af_transfers'):
    for value in range(256):
        for flags in (0, 1, 64, 255):
            cpu = x.CPU(random.Random(value * 257 + flags))
            cpu.put('a', value)
            cpu.put('b', value ^ 0xa5)
            cpu.put('f', flags)
            cpu.alt['f'] = flags ^ 0xa5 if name == 'alternate_store_flags' else flags
            ix, sp = cpu.get('ix'), cpu.sp
            cpu.run(program, entry=name)
            increment = 2 if name in ('competing_slots', 'mixed_af_transfers') else 0
            expected = (value + increment) & 255
            assert cpu.get('a') == expected, (name, value, flags)
            if name == 'competing_slots':
                assert cpu.mem[8194] == 42
            assert cpu.get('ix') == ix and cpu.sp == sp + 2
            if name == 'flags_live':
                assert bool(cpu.get('f') & 1) == (value < 128)
                assert bool(cpu.get('f') & 64) == (value == 128)
            if name == 'alternate_flags_live':
                assert cpu.alt['f'] == flags
            if name == 'mixed_af_transfers':
                assert cpu.get('f') & 1 and not cpu.get('f') & 64
            if name == 'alternate_store_flags':
                assert cpu.alt['f'] == flags ^ 0xa5
            if name == 'multiple_reads':
                assert cpu.mem[16384] == value
            if name == 'primary_live':
                assert cpu.get('c') == value ^ 0xa5
            checks += 1
print(f'Checked {checks} AF spill executions')
