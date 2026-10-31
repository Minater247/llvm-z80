"""Check every byte input and defined constant shift count in emitted code."""
import importlib.util
import random
import sys
from pathlib import Path

path = Path(__file__).with_name('z80-execution.py')
spec = importlib.util.spec_from_file_location('execution', path)
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
for op in ('shl', 'lshr', 'ashr'):
    for count in range(8):
        for value in range(256):
            cpu = execution.CPU(random.Random(value))
            cpu.put('e', value)
            ix, alternate = cpu.get('ix'), dict(cpu.alt)
            cpu.run(program, entry=op + str(count))
            signed = value if value < 128 else value - 256
            expected = (value << count if op == 'shl' else
                        value >> count if op == 'lshr' else signed >> count) & 255
            assert cpu.get('a') == expected, (op, count, value)
            assert cpu.sp == 0xf002 and cpu.get('ix') == ix
            assert cpu.alt == alternate
            assert cpu.writes <= set(range(0xef00, 0xf000))
print('Checked all 6144 constant byte shifts')
