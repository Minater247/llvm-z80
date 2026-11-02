"""Check spill reuse through joins, clobbering paths and loop-carried updates."""
import importlib.util
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
assembly = Path(sys.argv[1]).read_text()
rng = random.Random(1437)
checks = 0
for name in ('joins', 'clobbered_pair', 'clobbered_half', 'changed_slot',
             'preserved_call', 'clobbered_call', 'physical_reload', 'loop_update'):
    stub = 'opaque:\nld a, 15\n'
    if name == 'clobbered_call':
        stub += 'ld iy, 1234\n'
    program = execution.Program(assembly + stub + 'ret\n')
    for address in (0x2000, 0x20fe, 0x20ff, 0x7ffe):
        for condition in (0, 7, 128, 255):
            cpu = execution.CPU(rng)
            cpu.put('iy', address)
            cpu.put('a', condition)
            expected = address + (4 if name == 'loop_update' else 1)
            if name == 'changed_slot' and condition:
                expected = (condition << 8) | ((address + 1) & 255)
            # A rewritten high byte may select any memory page. Keep the
            # sampled byte away from the return address and spill frame.
            value = rng.randrange(256)
            cpu.mem[expected] = value
            sp, ix = cpu.sp, cpu.get('ix')
            cpu.run(program, entry=name)
            assert cpu.get('a') == value, (name, address, condition)
            assert cpu.sp == sp + 2 and cpu.get('ix') == ix
            checks += 1
print(f'{checks} spill-join executions passed')
