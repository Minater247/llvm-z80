"""Execute regional inlining and explicit inlining restrictions with bank wrapping."""
import importlib.util
from pathlib import Path
import random
import re
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)


class CPU(x.CPU):
    def __init__(self, seed):
        super().__init__(random.Random(seed))
        self.trace = []

    def read(self, operand, width=1):
        if operand.startswith('(') and self.addr(operand) == 8960:
            self.trace.append(('r', 8960, 1))
            return 1
        return super().read(operand, width)

    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) in (8960, 8961):
            self.trace.append(('w', self.addr(operand), value & 255))
        super().write(operand, value, width)


checks = 0
for allocator in ('z80', 'greedy'):
    for variant in ('inline', 'noinline-leaf', 'noinline-call'):
        text = Path(source).read_text()
        if variant == 'noinline-leaf':
            text = text.replace('@step() nounwind', '@step() nounwind noinline')
        elif variant == 'noinline-call':
            text = text.replace('call fastcc void @step()', 'call fastcc void @step() noinline')
        ir = directory / f'{allocator}-{variant}.ll'
        ir.write_text(text)
        assembly = subprocess.check_output([
            llc, '-regalloc='+allocator, '-verify-machineinstrs',
            '-mattr=+alternate-regs', str(ir),
            '-o', '-'], text=True)
        ir.with_suffix('.s').write_text(assembly)
        assert ('step.address:' not in assembly) == (variant == 'inline')
        program = x.Program(re.sub(r'\bcursor\b', '8192', assembly))
        for page in (0x1200, 0x8100):
            for low in (0, 1, 248, 250, 251, 254, 255):
                cpu = CPU(low)
                start = page | low
                cpu.put('de', start)
                samples = [cpu.mem[page | ((low+i)&255)] for i in range(6)]
                expected = []
                for sample in samples:
                    expected += [('r', 8960, 1), ('w', 8960, 39),
                                 ('w', 8961, 17), ('w', 8960, 42),
                                 ('w', 8961, sample)]
                ix = cpu.get('ix')
                cpu.run(program, entry='drive')
                assert cpu.trace == expected, (allocator, variant, start)
                assert cpu.get('hl') == page | ((low+6)&255)
                assert cpu.sp == 0xf002 and cpu.get('ix') == ix
                checks += 1
print(f'{checks} regional-leaf executions passed')
