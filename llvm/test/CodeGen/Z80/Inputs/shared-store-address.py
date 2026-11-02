"""Check repeated stores, preserved byte lanes and occupied register pairs."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
class TracedCPU(x.CPU):
    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) == 16384:
            self.events.append((16384, value & 255))
        super().write(operand, value, width)

program = x.Program(Path(sys.argv[1]).read_text())
for name in ('shared', 'preserved_pairs', 'pressure', 'partial_pair'):
    for value in range(256):
        cpu = TracedCPU(random.Random(value))
        cpu.events = []
        cpu.put('a', value)
        before = {r: cpu.get(r) for r in ('bc', 'de', 'hl', 'ix', 'iy')}
        cpu.run(program, entry=name)
        assert cpu.mem[16384] == 12
        assert cpu.events == [(16384, value), (16384, 12)]
        assert cpu.sp == 0xf002
        expected = dict(before)
        if name == 'shared': expected['de'] = 12345
        elif name == 'pressure': expected.update(hl=12345, bc=23456, de=34567)
        elif name == 'partial_pair': expected['de'] = 123 * 256 + (before['de'] & 255)
        assert all(cpu.get(r) == v for r, v in expected.items()), (name, value)
print('1024 repeated-store executions passed')
