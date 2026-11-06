"""Check address sharing across branches, cast duplication and overlapping bits."""
import importlib.util
from pathlib import Path
import random
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)


class TraceCPU(x.CPU):
    def __init__(self, seed):
        super().__init__(random.Random(seed))
        self.trace = []

    def read(self, operand, width=1):
        value = super().read(operand, width)
        if operand.startswith('(') and 8960 <= self.addr(operand) < 8968:
            self.trace.append(('r', self.addr(operand), value))
        return value

    def write(self, operand, value, width=1):
        if operand.startswith('(') and 8960 <= self.addr(operand) < 8968:
            self.trace.append(('w', self.addr(operand), value & 255))
        super().write(operand, value, width)


checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        for overlap in (False, True):
            ir = directory / f'{allocator}-{bank}-{overlap}.ll'
            text = Path(source).read_text()
            if overlap:
                text = text.replace('and i16 %input, 6', 'and i16 %input, 7')
            ir.write_text(text)
            flags = [llc, '-regalloc='+allocator, '-verify-machineinstrs',
                     '-mattr='+('+' if bank else '-')+'alternate-regs', str(ir)]
            prepared = subprocess.check_output(
                [*flags, '-stop-before=irtranslator', '-o', '-'], text=True)
            assert ('adjacent.address = getelementptr' in prepared) != overlap
            assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
            program = x.Program(assembly)
            for base in (0, 1, 2, 3, 7, 255, 65535):
                first = 8960 | (base & (7 if overlap else 6))
                second = first | 1
                for value in range(0, 256, 7):
                    for function in ('split', 'single'):
                        cpu = TraceCPU(value)
                        cpu.put('de', base)
                        cpu.put('c', value)
                        ix = cpu.get('ix')
                        cpu.run(program, entry=function)
                        next_value = (value+(1 if value < 128 else 2)) & 255
                        expected = [('w', first, value)]
                        if function == 'split':
                            expected += [('w', second, next_value),
                                         ('r', second, next_value),
                                         ('w', first, next_value)]
                        else:
                            expected += [('w', second, value ^ 255)]
                        assert cpu.trace == expected, (function, base, value, cpu.trace)
                        assert cpu.get('ix') == ix and cpu.sp == 0xf002
                        checks += 1
print(f'{checks} split-address executions passed')
