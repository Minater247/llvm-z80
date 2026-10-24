"""Check byte comparisons, including loads separated from tests by stores."""
import importlib.util
import random
import re
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execute', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
asm = Path(sys.argv[1]).read_text()
conditions = {
    'bit_zero': lambda value: not value & 4,
    'bit_nonzero': lambda value: bool(value & 128),
    'negative': lambda value: bool(value & 128),
    'nonnegative': lambda value: not value & 128,
    'negative_sle': lambda value: bool(value & 128),
    'nonnegative_sgt': lambda value: not value & 128,
    'bit_before_store': lambda value: bool(value & 4),
    'sign_before_store': lambda value: bool(value & 128),
}
register_inputs = {'negative', 'negative_sle', 'nonnegative_sgt'}
for name, condition in conditions.items():
    body = re.search(r'^' + name + r':.*?^\s*\.size', asm, re.M | re.S)[0]
    program = x.Program(body)
    for value in range(256):
        cpu = x.CPU(random.Random(value))
        cpu.put('de', 0x2100)
        cpu.mem[0x2100] = value
        if name in register_inputs:
            cpu.put('e', value)
        cpu.run(program)
        expected = condition(value)
        assert cpu.get('a') == expected, (name, value, cpu.get('a'), expected)
        if name not in register_inputs:
            assert cpu.reads.count(0x2100) == 1, (name, 'volatile reads')
        if name.endswith('before_store'):
            assert cpu.mem[0x2100] == 0
print('Checked 2048 bit conditions')
