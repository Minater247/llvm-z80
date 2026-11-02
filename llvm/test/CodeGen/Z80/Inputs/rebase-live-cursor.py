"""Check record walks and field updates across wraparound and loop backedges."""
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
for name, step, field in [('forward', 17, 3), ('backward', -17, 5)]:
    for base in (0x2100, 0xffe8, 7):
        for count in range(1, 17):
            for value in (0, 1, 127, 128, 254, 255):
                cpu = x.CPU(random.Random(count * 256 + value))
                cpu.put('de', base)
                cpu.put('a', count)
                expected = {}
                for i in range(1, count + 1):
                    address = (base + i * step) & 65535
                    cpu.mem[address] = (value + i) & 255
                    cpu.mem[(address + field) & 65535] = value
                    expected[(address + field) & 65535] = (
                        value | 4 if name == 'forward' else (value - 1) & 255)
                cpu.run(program, entry=name)
                assert cpu.get('a') == (value + count) & 255, (name, base, count)
                assert all(cpu.mem[a] == v for a, v in expected.items())
                assert cpu.sp == 0xf002
                checks += 1
print('Checked', checks, 'record walks')

for name in ('changed_cursor', 'changed_base', 'wide_displacement'):
    for base in (0x2100, 0xffe8, 7):
        for value in range(256):
            cpu = x.CPU(random.Random(value))
            cpu.put('de', base)
            address = (8192 + 17 if name == 'changed_base' else
                       base + (126 if name == 'wide_displacement' else 17)) & 65535
            cpu.mem[address] = value
            cpu.run(program, entry=name)
            cursor = (8192 if name == 'changed_cursor' else
                      base + (-17 if name == 'wide_displacement' else 17)) & 65535
            assert cpu.get('a') == value, (name, base, value)
            assert cpu.get('hl') == cursor
print('Checked 2304 unchanged reaching-value and displacement cases')

for name, step in [('next_cursor', 2), ('previous_cursor', -2)]:
    for base in (0x2100, 0xfff0, 7):
        for count in range(1, 17):
            cpu = x.CPU(random.Random(count))
            cpu.put('de', base)
            cpu.put('a', count)
            last = (base + step * count) & 65535
            expected = cpu.mem[last]
            cpu.run(program, entry=name)
            assert cpu.get('a') == expected, (name, base, count)
            assert cpu.get('de') == (last - step // 2) & 65535
            assert cpu.sp == 0xf002
print('Checked 96 consecutive cursor updates across blocks')

for base in (0, 0xffec, 0xffee, 0xffef, 0xffff):
    cpu = x.CPU(random.Random(base))
    cpu.put('de', base)
    cpu.run(program, entry='live_carry')
    assert cpu.get('a') == (31 if base + 20 > 65535 else 79)
    assert cpu.get('hl') == (base + 17) & 65535
print('Checked live carry across rebased address candidates')

for name, step, offset in [('far_forward', 1024, 1031),
                           ('far_backward', -1024, -1021)]:
    for base in (0, 7, 0x2100, 0xffe8):
        for value in range(256):
            cpu = x.CPU(random.Random(value))
            cpu.put('de', base)
            cpu.put('a', value)
            address = (base + offset) & 65535
            expected = cpu.mem[address]
            cpu.run(program, entry=name)
            assert cpu.get('a') == expected, (name, base, value)
            assert cpu.mem[(base + step) & 65535] == value
            assert cpu.sp == 0xf002
print('Checked 2048 distant-base updates across blocks')
