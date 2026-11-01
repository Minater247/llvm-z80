"""Check zero-test reuse while retaining distinct parity and carry semantics."""
import importlib.util
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(9921)
for function in ('zero_add', 'zero_copy', 'zero_cp', 'distinct_or', 'parity',
                 'carry', 'flags_overwritten'):
    for value in range(256):
        for other in (0, 1, 128, 255):
            cpu = execution.CPU(rng)
            cpu.sp = 0xf000
            cpu.put('a', value)
            cpu.put('b', other)
            result = (value + 1) & 255
            if function.startswith('zero_'):
                expected = int(value == 1)
            elif function == 'distinct_or':
                expected = int((result | other) == 0)
            elif function == 'parity':
                expected = int(result.bit_count() % 2 == 0)
            elif function == 'carry':
                expected = result
            else:
                expected = int(result == 0)
            cpu.run(program, entry=function)
            assert cpu.get('a') == expected, (function, value, other,
                                              cpu.get('a'), expected)
            assert cpu.sp == 0xf002
print('Checked 7168 accumulator-flag executions')
