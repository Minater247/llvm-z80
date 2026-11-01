"""Check nested scratch saves, frame addresses, flags and live pair values."""
import argparse
import importlib.util
import random
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--llc', required=True)
p.add_argument('--mir', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
rng = random.Random(373)
checks = 0
for target in ('z80', 'z180'):
    for bank in (False, True):
        asm = subprocess.check_output([
            a.llc, '-start-before=prologepilog', '-mcpu=' + target,
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            '-verify-machineinstrs', a.mir, '-o', '-'], text=True)
        (a.work / (target + str(bank) + '.s')).write_text(asm)
        program = execution.Program(asm)
        # The large frame reserves two additional emergency word slots.
        for name, size in (('pairs', 4), ('flags', 264)):
            for _ in range(32):
                cpu = execution.CPU(rng)
                cpu.sp = rng.randrange(0x9000, 0xf000)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                assert [cpu.get(r) for r in ('bc', 'hl', 'iy', 'a')] == [
                    0x1122, 0x3344, 0x5566, 119], (target, bank, name, cpu.r)
                assert cpu.get('de') == sp - size - 2
                if name == 'flags':
                    assert cpu.get('f') == 0x42
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.alt == alternate
                assert cpu.writes <= set(range(sp - size - 32, sp))
                checks += 1
print(f'{checks} nested scratch-save executions passed')
