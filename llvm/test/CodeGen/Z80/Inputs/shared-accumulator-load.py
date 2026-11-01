"""Check shared byte reads, comparisons and stores for every input byte."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
cpu = execution.CPU(random.Random(5217))
checks = 0

for address in (0x1200, 0xffff):
    for value in range(256):
        for alias in (False, True):
            output = address if alias else 0x3000
            cpu.sp = 0xf000
            cpu.mem[output] = 0xa5
            cpu.mem[address] = value
            cpu.put('de', address)
            cpu.put('bc', output)
            cpu.reads.clear()
            cpu.run(program, entry='classify')
            expected = 0 if value < 12 else 3 if value >= 128 else 1 if value < 97 else 2
            assert cpu.get('a') == expected, (value, cpu.get('a'), expected)
            assert cpu.mem[output] == (value if value < 12 or alias else 0xa5)
            assert cpu.reads.count(address) == 1
            assert cpu.sp == 0xf002
            checks += 1

        cpu.sp = 0xf000
        cpu.mem[address] = value
        cpu.mem[0x3000] = cpu.mem[0x4000] = value ^ 255
        cpu.put('de', address)
        cpu.put('bc', 0x3000)
        cpu.put('hl', 0x4000)
        cpu.reads.clear()
        cpu.run(program, entry='two_stores')
        assert cpu.mem[0x3000] == cpu.mem[0x4000] == value
        assert cpu.reads.count(address) == 1
        assert cpu.sp == 0xf002
        checks += 1

        for other in (0, 1, 12, 30, 31, 127, 128, 255):
            cpu.sp = 0xf000
            cpu.mem[address] = value
            cpu.put('de', address)
            cpu.put('c', other)
            cpu.reads.clear()
            cpu.run(program, entry='other_compare')
            expected = 0 if other < value else 1 if value < 31 else 2
            assert cpu.get('a') == expected, (value, other, cpu.get('a'), expected)
            assert cpu.reads.count(address) == 1
            assert cpu.sp == 0xf002
            checks += 1

print(f'{checks} shared byte load executions passed')
