"""Check every byte position of wide equality, including live result operands."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
program = x.Program(Path(sys.argv[1]).read_text())
cpu = x.CPU(random.Random(247))
checks = 0
for name, constant in [('mixed', 0x12345678), ('zero', 0),
                       ('all_ones', 0xffffffff), ('retained', 0xffffff)]:
    for lane in range(4):
        for byte in range(256):
            value = (constant & ~(255 << (8 * lane))) | (byte << (8 * lane))
            for i in range(4):
                cpu.mem[0x2100 + i] = (value >> (i * 8)) & 255
            cpu.sp = 0xf000
            cpu.put('de', 0x2100)
            cpu.run(program, entry=name)
            if name == 'retained':
                expected = 37 if value == constant else (value ^ (value >> 16)) & 65535
                result = cpu.get('hl')
            else:
                condition = value != constant if name == 'zero' else value == constant
                expected = 31 if condition else 79
                result = cpu.get('a')
            assert result == expected, (name, lane, byte, result, expected)
            assert cpu.sp == 0xf002
            checks += 1
print('Checked', checks, 'wide constant equalities and retained operands')
