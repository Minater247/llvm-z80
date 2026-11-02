"""Check record displacement endpoints and wrapping 16-bit addresses."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(128127)
checks = 0
for base in (0, 0x1234, 0x7ff0, 0xff00):
    for value in range(256):
        cpu = execution.CPU(rng)
        cpu.put('de', base)
        cpu.put('c', value)
        source = rng.randrange(256)
        cpu.mem[(base + 328) & 65535] = source
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry='fields')
        assert cpu.mem[(base + 144) & 65535] == value
        assert cpu.mem[(base + 399) & 65535] == source ^ value
        assert cpu.get('a') == source ^ value
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        checks += 1
print(f'{checks} shared record-base executions passed')
