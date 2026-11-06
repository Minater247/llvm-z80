"""Check shared adjacent addresses, wraparound and ordered byte accesses."""
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
    def __init__(self, seed, addresses):
        super().__init__(random.Random(seed))
        self.addresses = addresses
        self.trace = []

    def read(self, operand, width=1):
        value = super().read(operand, width)
        if operand.startswith('(') and self.addr(operand) in self.addresses:
            assert width == 1
            self.trace.append(('r', self.addr(operand), value))
        return value

    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) in self.addresses:
            assert width == 1
            self.trace.append(('w', self.addr(operand), value & 255))
        super().write(operand, value, width)


checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        for kind in ('or', 'reverse', 'positive', 'negative', 'overlap'):
            text = Path(source).read_text()
            if kind == 'reverse':
                text = text.replace('8960', 'FIRST').replace('8961', '8960').replace('FIRST', '8961')
            elif kind == 'overlap':
                text = text.replace('and i16 %input, 6', 'and i16 %input, 7')
            elif kind in ('positive', 'negative'):
                step = 1 if kind == 'positive' else -1
                text = text.replace('%first = or i16 %offset, 8960', '%first = add i16 %input, 0')
                text = text.replace('%second = or i16 %offset, 8961', f'%second = add i16 %first, {step}')
                text = text.replace('%q = inttoptr i16 %second to ptr', f'%q = getelementptr i8, ptr %p, i16 {step}')
            ir = directory / f'{allocator}-{bank}-{kind}.ll'
            ir.write_text(text)
            flags = [llc, '-regalloc='+allocator,
                     '-mattr='+('+' if bank else '-')+'alternate-regs',
                     '-verify-machineinstrs', str(ir)]
            prepared = subprocess.check_output([*flags, '-stop-before=irtranslator', '-o', '-'], text=True)
            if kind in ('or', 'reverse'):
                assert 'adjacent.address = getelementptr' in prepared
            elif kind == 'overlap':
                assert 'adjacent.address = getelementptr' not in prepared
            selected = subprocess.check_output([*flags, '-stop-after=instruction-select', '-o', '-'], text=True)
            if kind != 'overlap':
                assert 'ST8_ADJ' in selected, (allocator, bank, kind)
            assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
            program = x.Program(assembly)
            for base in (0, 1, 254, 255, 256, 8191, 8192, 65534, 65535):
                if kind in ('or', 'reverse'):
                    first = 8960 + (base & 6) + (kind == 'reverse')
                elif kind == 'overlap':
                    first = 8960 + (base & 7)
                else:
                    first = base
                second = (first + (-1 if kind in ('reverse', 'negative') else 1)) & 65535
                if kind == 'overlap':
                    second = first | 1
                for value in range(0, 256, 7):
                    cpu = TraceCPU(value, (first, second))
                    cpu.put('de', base)
                    cpu.put('c', value)
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry='run')
                    read_first = (value+1)&255 if first == second else value
                    expected = [('w', first, value), ('w', second, (value+1)&255),
                                ('r', first, read_first), ('w', first, (value+2)&255),
                                ('w', second, (value+3)&255), ('r', second, (value+3)&255)]
                    assert cpu.trace == expected, (allocator, bank, kind, base, value, cpu.trace)
                    assert cpu.get('hl') == ((read_first + ((value+3)&255)) ^ first)
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    checks += 1
print(f'{checks} adjacent-address executions passed')
