"""Check nonempty, empty and early-aborting countdowns with exact bus traces."""
import importlib.util
from pathlib import Path
import random
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)


class CPU(x.CPU):
    def __init__(self, seed):
        super().__init__(random.Random(seed))
        self.trace = []

    def read(self, operand, width=1):
        value = super().read(operand, width)
        if operand.startswith('(') and 0x3000 <= self.addr(operand) < 0x3100:
            self.trace.append(('read', self.addr(operand), value))
        return value

    def write(self, operand, value, width=1):
        if operand.startswith('(') and 0x4000 <= self.addr(operand) < 0x4100:
            self.trace.append(('write', self.addr(operand), value & 255))
        super().write(operand, value, width)


llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        for nonempty in (False, True):
            text = Path(source).read_text()
            if not nonempty:
                text = text.replace('%start = add nuw i8 %bounded, 1',
                                    '%start = add i8 %count, 0')
            ir = directory / f'{allocator}-{bank}-{nonempty}.ll'
            ir.write_text(text)
            programs = []
            for enabled in (False, True):
                flags = [llc, '-regalloc='+allocator, '-verify-machineinstrs',
                         '-mattr='+('+' if bank else '-')+'alternate-regs',
                         '-z80-rotate-counted-loops='+str(enabled).lower(), str(ir)]
                prepared = subprocess.check_output(
                    [*flags, '-stop-before=irtranslator', '-o', '-'], text=True)
                # The guarded body becomes the header only when entry is proven.
                assert ('icmp eq i8 %next, 0' in prepared) == (enabled and nonempty)
                assert ('icmp eq i16 %next, 0' in prepared) == enabled
                assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
                programs.append(x.Program(assembly))
            for count in range(256):
                for abort_at in (0, 3, 255):
                    length = (count & 7) + 1 if nonempty else count
                    used = 0 if count == 255 else min(length, abort_at + 1)
                    expected = []
                    for i in range(used):
                        expected += [('write', 0x4000+i, length-i),
                                     ('read', 0x3000+i, int(i != abort_at))]
                    result = 99 if count == 255 else length-abort_at if abort_at < length else 0
                    for program in programs:
                        cpu = CPU(count)
                        cpu.mem[0x3000:0x3100] = bytes([1]) * 256
                        cpu.mem[0x3000+abort_at] = 0
                        cpu.put('e', count)
                        sp, ix = cpu.sp, cpu.get('ix')
                        cpu.run(program, entry='run')
                        assert cpu.trace == expected, (allocator, bank, nonempty, count, abort_at)
                        assert cpu.get('a') == result
                        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                        checks += 1
            for count in (0, 1, 2, 254, 255, 256, 257, 1023):
                for program in programs:
                    cpu = CPU(count)
                    cpu.put('de', count)
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry='word')
                    assert cpu.trace == [('write', 0x4000, n & 255)
                                         for n in range(count | 1, 0, -1)]
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    checks += 1
print(f'{checks} countdown executions passed')
