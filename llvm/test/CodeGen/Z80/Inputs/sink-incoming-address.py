"""Execute delayed address copies with repeated polling and register clobbers."""
import argparse
import importlib.util
from pathlib import Path
import random
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--llc', required=True)
parser.add_argument('--mir', type=Path, required=True)
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
args.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)

class CPU(execution.CPU):
    def read(self, operand, width=1):
        if operand.startswith('(') and self.addr(operand) == 0x4000:
            self.polls += 1
            return 128 if self.polls < 4 else 0
        return super().read(operand, width)

rng = random.Random(4931)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        assembly = subprocess.check_output([
            args.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(args.mir), '-o', '-'], text=True)
        assembly += '\nopaque:\nld de, 12345\nld bc, 23456\nld hl, 34567\nld a, 231\nret\n'
        (args.work / (allocator + str(bank) + '.s')).write_text(assembly)
        program = execution.Program(assembly)
        for name in ('poll_address', 'clobbered_lane', 'intervening_call', 'earlier_use'):
            for address in (0x3000, 0x30ff, 0xfffe, 0xffff):
                cpu = CPU(rng)
                cpu.polls = 0
                cpu.put('de', address)
                before, sp, ix, alternate = bytes(cpu.mem), cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                expected = bytearray(before)
                expected[address] = 17
                expected[(address + 1) & 65535] = 34
                assert cpu.polls == 4, (allocator, bank, name, cpu.polls)
                assert cpu.mem[:sp - 256] == expected[:sp - 256]
                assert cpu.mem[sp + 2:] == expected[sp + 2:]
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} polling address executions passed')
