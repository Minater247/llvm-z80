"""Check byte-constructed pointers, retained byte users and later updates."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(2648)
count = 0
for low in range(256):
    for high in (0x20, 0x3f, 0x40, 0x7f):
        for name in ('constructed', 'byte_user', 'updated_byte'):
            cpu = execution.CPU(rng)
            pointer = high * 256 + low
            cpu.put('de', pointer)
            address = high * 256 + ((low + 1) & 255) if name == 'updated_byte' else pointer
            value = rng.randrange(256)
            cpu.mem[address] = value
            preserved = {r: cpu.get(r) for r in ('bc', 'de', 'ix')}
            sp = cpu.sp
            cpu.run(program, entry=name)
            assert cpu.get('a') == value, (name, pointer, cpu.get('a'), value)
            assert cpu.reads.count(address) == 1
            assert cpu.sp == sp + 2
            assert cpu.writes <= set(range(sp - 256, sp))
            if name == 'constructed':
                assert cpu.get('hl') == 42
                for r, expected in preserved.items():
                    assert cpu.get(r) == expected, (name, r)
            if name == 'byte_user':
                assert cpu.get('c') == low
            count += 1
print(f'{count} constructed-pointer executions passed')
