"""Check exact write counts and visible word values through loop termination."""
import argparse
import importlib.util
import random
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--llc', required=True)
p.add_argument('--source', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)

class CPU(execution.CPU):
    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) in (0x6000, 0x6001):
            self.output.extend((value >> (8 * i)) & 255 for i in range(width))
        super().write(operand, value, width)

counts = [1, 2, 3, 7, 15, 16, 31, 64, 112, 127, 128, 129, 254, 255, 256, 257, 511]
cases = [('count_' + str(n), [73] * n) for n in counts]
cases += [('step_two', [73] * 56), ('continue_on_overflow', [73] * 2)]
cases += [('visible_counter', [b for n in range(65536 - 112, 65536)
                               for b in (n & 255, n >> 8)])]
rng = random.Random(151)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        asm = subprocess.check_output([
            a.llc, '-regalloc=' + allocator,
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            '-verify-machineinstrs', a.source, '-o', '-'], text=True)
        (a.work / (allocator + str(bank) + '.s')).write_text(asm)
        program = execution.Program(asm)
        for name, expected in cases:
            cpu = CPU(rng)
            cpu.output = []
            sp, ix = cpu.sp, cpu.get('ix')
            cpu.run(program, entry=name)
            assert cpu.output == expected, (allocator, bank, name, cpu.output)
            assert cpu.sp == sp + 2 and cpu.get('ix') == ix
            assert cpu.writes <= set(range(sp - 256, sp)) | {0x6000, 0x6001}
            checks += 1
print(f'{checks} complete counter-loop executions passed')
