"""Check byte-select narrowing for every condition and both allocator modes."""
import argparse
import importlib.util
from pathlib import Path
import random
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--llc', required=True)
parser.add_argument('--ir', required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        assembly = subprocess.check_output([
            args.llc, '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            args.ir, '-o', '-'], text=True)
        program = execution.Program(assembly)
        rng = random.Random(901)
        for condition in range(256):
            for name, expected in (
                    ('low_byte', 292 if condition < 83 else 260),
                    ('high_byte', 0x8101 if condition >= 128 else 0x4201),
                    ('both_bytes', 292 if condition == 0 else 515),
                    ('selected_pointer', None)):
                cpu = execution.CPU(rng)
                cpu.put('e', condition)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                if expected is None:
                    address = 292 if condition > 172 else 260
                    expected = cpu.mem[address]
                cpu.run(program, entry=name)
                result = cpu.get('a' if name == 'selected_pointer' else 'hl')
                assert result == expected, (allocator, bank, name, condition)
                if name == 'selected_pointer':
                    assert cpu.reads.count(address) == 1
                    assert (260 if address == 292 else 292) not in cpu.reads
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.writes <= set(range(sp - 256, sp))
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} constant byte-select executions passed')
