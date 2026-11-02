"""Check constant-address polling and caller pointer preservation."""
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
        if operand.startswith('(') and self.addr(operand) == 8192:
            self.events.append((8192, value & 255))
        super().write(operand, value, width)

program = x.Program(Path(sys.argv[1]).read_text())
for value in range(256):
    cpu = TracedCPU(random.Random(value))
    cpu.events = []
    cpu.put('de', 0x9000 + value)
    cpu.mem[0x9000 + value] = value
    cpu.mem[8192] = 1
    ix = cpu.get('ix')
    cpu.run(program, entry='caller')
    assert cpu.get('a') == value and cpu.mem[8192] == 42
    assert cpu.events == [(8192, 39), (8192, 41), (8192, 42)]
    assert cpu.sp == 0xf002 and cpu.get('ix') == ix
print('256 constant-address caller executions passed')
