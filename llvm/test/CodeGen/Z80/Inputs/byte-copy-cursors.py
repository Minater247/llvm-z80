"""Check byte-copy cursors, overlapping buffers, wrapping and volatile order."""
import importlib.util
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)


class TraceCPU(execution.CPU):
    def __init__(self, rng):
        super().__init__(rng)
        self.trace = []

    def read(self, operand, width=1):
        if operand.startswith('(') and not 0xef00 <= self.addr(operand) < 0xf002:
            self.trace.append(('read', self.addr(operand), width))
        return super().read(operand, width)

    def write(self, operand, value, width=1):
        if operand.startswith('(') and not 0xef00 <= self.addr(operand) < 0xf002:
            self.trace.append(('write', self.addr(operand), width))
        super().write(operand, value, width)


program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(6208)
checks = 0
for name, step in [('forward', 1), ('backward', -1), ('ordered', 1),
                   ('integer_forward', 1), ('integer_backward', -1),
                   ('integer_ordered', 1), ('large_forward', 1),
                   ('large_backward', -1), ('large_ordered', 1),
                   ('large_integer', 1)]:
    for source in [0, 1, 0x21fe, 0x21ff, 0x2200, 0xfffe, 0xffff]:
        for displacement in [-3, -1, 0, 1, 3, 0x4000]:
            destination = (source + displacement) & 65535
            cpu = TraceCPU(rng)
            offset = 4096*step if name.startswith('large_') else 0
            cpu.put('de', (source-offset)&65535)
            cpu.put('bc', (destination-offset)&65535)
            expected = bytearray(cpu.mem)
            trace = []
            destinations = set()
            for i in range(4):
                src, dst = (source + step*i) & 65535, (destination + step*i) & 65535
                expected[dst] = expected[src]
                destinations.add(dst)
                trace.extend([('read', src, 1), ('write', dst, 1)])
            ix = cpu.get('ix')
            cpu.run(program, entry=name)
            assert cpu.mem[:0xef00] == expected[:0xef00], (name, source, displacement)
            assert cpu.mem[0xf002:] == expected[0xf002:], (name, source, displacement)
            assert cpu.writes <= destinations | set(range(0xef00, 0xf000))
            assert cpu.sp == 0xf002 and cpu.get('ix') == ix
            if name.endswith('ordered'):
                assert cpu.trace == trace, (name, source, displacement, cpu.trace)
            checks += 1
print(f'Checked {checks} byte-copy executions')
