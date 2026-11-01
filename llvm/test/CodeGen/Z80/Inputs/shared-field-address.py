"""Execute shared field accesses and check results and neighboring bytes."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location('execution', Path(__file__).with_name('z80-execution.py'))
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)
program = e.Program(Path(sys.argv[1]).read_text())
rng = random.Random(9164)
count = 0
for value in range(256):
    for name, offset in [('increment', 23), ('add_field', 22),
                         ('negative_field', -6), ('last_field', 127),
                         ('field_only', 23), ('outside_field', 128)]:
        cpu = e.CPU(rng)
        base = 0x5000 + rng.randrange(256)
        data = rng.randbytes(258)
        cpu.mem[base-128:base+130] = data
        prefix = rng.randrange(256)
        delta = rng.randrange(256)
        cpu.mem[base+3] = prefix
        cpu.mem[base+offset] = value
        expected = bytearray(cpu.mem[base-128:base+130])
        result = {'increment': value+1, 'add_field': value+delta,
                  'negative_field': value^16, 'last_field': value-1,
                  'field_only': value+1, 'outside_field': value+1}[name] & 255
        expected[offset+128] = result
        cpu.put('de', base)
        cpu.put('c', delta)
        cpu.run(program, entry=name)
        assert cpu.get('a') == (result if name == 'field_only' else result ^ prefix), (name, value, delta)
        assert cpu.mem[base-128:base+130] == expected, (name, value, 'memory')
        assert cpu.sp == 0xf002, (name, 'stack')
        count += 1
print('Checked', count, 'shared-field updates and neighboring memory')
