"""Check sparse switch membership for every byte and word input."""
import importlib.util
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
cpu = execution.CPU(random.Random(187))
initial, alternate = dict(cpu.r), dict(cpu.alt)
checks = 0
for name, bits, members in [
        ('byte_set', 8, {1, 4, 6, 9, 12, 14}),
        ('word_set', 16, {65532, 65535, 2, 5, 8, 10})]:
    for value in range(1 << bits):
        cpu.r, cpu.alt = dict(initial), dict(alternate)
        cpu.put('e' if bits == 8 else 'de', value)
        cpu.sp = 0xf000
        cpu.writes.clear()
        cpu.reads.clear()
        cpu.cycles = 0
        cpu.run(program, entry=name)
        assert cpu.get('a') == (value in members), (name, value, cpu.get('a'))
        assert cpu.sp == 0xf002
        assert cpu.get('ix') == initial['ixh'] * 256 + initial['ixl']
        assert cpu.writes <= set(range(0xef00, 0xf000))
        checks += 1
print(f'Checked all {checks} switch inputs')
