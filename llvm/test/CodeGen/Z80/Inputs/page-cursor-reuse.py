"""Check constant-page cursors, overlap, byte wrapping and access ordering."""
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
rng = random.Random(24739)
checks = 0
for name, step in [('forward', 1), ('backward', -1), ('ordered', 1),
                   ('different_pages', 1), ('wide_base', 1)]:
    for start in range(256):
        for displacement in [-3, -1, 0, 1, 3, 0x4000]:
            source = (0x3100 + start + displacement) & 65535
            cpu = TraceCPU(rng)
            cpu.put('de', source)
            cpu.put('c', start)
            expected = bytearray(cpu.mem)
            trace = []
            destinations = set()
            for i in range(4):
                src = (source + step*i) & 65535
                page = 0x3100 + (256*(i%2) if name == 'different_pages' else 0)
                dst = page | ((start + step*i) & 255)
                expected[dst] = expected[src]
                destinations.add(dst)
                trace.extend([('read', src, 1), ('write', dst, 1)])
            ix = cpu.get('ix')
            cpu.run(program, entry=name)
            assert cpu.mem[:0xef00] == expected[:0xef00], (name, start, displacement)
            assert cpu.mem[0xf002:] == expected[0xf002:], (name, start, displacement)
            assert cpu.writes <= destinations | set(range(0xef00, 0xf000))
            assert cpu.sp == 0xf002 and cpu.get('ix') == ix
            if name in ('ordered', 'different_pages'):
                assert cpu.trace == trace, (name, start, displacement, cpu.trace)
            checks += 1
for start in range(256):
    for source in [0x3100 | start, 0x3100 | ((start+1)&255), 0x7100]:
        cpu = TraceCPU(rng)
        cpu.put('de', source)
        cpu.put('c', start)
        expected = bytearray(cpu.mem)
        trace = []
        destinations = {source}
        for i in range(3):
            dst = 0x3100 | ((start+i)&255)
            if i:
                expected[source] = (start+i)&255
                trace.append(('write', source, 1))
            expected[dst] = expected[source]
            destinations.add(dst)
            trace.extend([('read', source, 1), ('write', dst, 1)])
        ix = cpu.get('ix')
        cpu.run(program, entry='shared_byte')
        assert cpu.mem[:0xef00] == expected[:0xef00], ('shared_byte', start, source)
        assert cpu.mem[0xf002:] == expected[0xf002:]
        assert cpu.writes <= destinations | set(range(0xef00, 0xf000))
        assert cpu.sp == 0xf002 and cpu.get('ix') == ix
        assert cpu.trace == trace, ('shared_byte', start, source, cpu.trace)
        checks += 1
for name, value in [('fill_zero', 0), ('fill_value', 173)]:
    for start in range(256):
        cpu = TraceCPU(rng)
        cpu.put('c', start)
        expected = bytearray(cpu.mem)
        destinations = [0x3100 | ((start + i) & 255) for i in range(8)]
        for dst in destinations:
            expected[dst] = value
        ix, alternate = cpu.get('ix'), dict(cpu.alt)
        cpu.run(program, entry=name)
        assert cpu.mem[:0xef00] == expected[:0xef00], (name, start)
        assert cpu.mem[0xf002:] == expected[0xf002:]
        assert cpu.writes <= set(destinations) | set(range(0xef00, 0xf000))
        assert cpu.trace == [('write', dst, 1) for dst in destinations]
        assert cpu.sp == 0xf002 and cpu.get('ix') == ix
        assert cpu.alt == alternate
        checks += 1
print(f'Checked {checks} page-cursor executions')
