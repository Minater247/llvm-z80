import importlib.util
import random
import sys
from pathlib import Path
spec = importlib.util.spec_from_file_location('execute', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
p = x.Program(Path(sys.argv[1]).read_text())
for name, delta in [('increment', 1), ('decrement', -1)]:
    for value in range(256):
        c = x.CPU(random.Random(value))
        c.put('de', 0x2100)
        c.mem[0x2100] = value
        c.run(p, entry=name)
        assert c.get('a') == c.mem[0x2100] == (value + delta) & 255
checks = 512
for name, delta, offset, invert in [('increment_zero', 1, 0, False),
                                    ('decrement_zero', -1, 0, True),
                                    ('offset_zero', -1, 5, False)]:
    for base in (0x2100, 0xfffb):
        for value in range(256):
            c = x.CPU(random.Random(value))
            c.put('de', base)
            address = (base + offset) & 65535
            c.mem[address] = value
            c.run(p, entry=name)
            result = (value + delta) & 255
            assert c.mem[address] == result
            assert c.get('a') == (17 if (result == 0) != invert else 93)
            assert c.reads.count(address) == 1, (name, c.reads)
            checks += 1
print('Checked', checks, 'memory updates, results and zero branches')

for value in range(256):
    class HardwareCPU(x.CPU):
        def instruction_complete(self):
            if 0x2100 in self.writes:
                self.mem[0x2100] = 0 if value & 1 else 73
    c = HardwareCPU(random.Random(value))
    c.put('de', 0x2100)
    c.mem[0x2100] = value
    c.run(p, entry='ordered_reread')
    assert c.get('a') == (17 if value & 1 else 93)
    assert c.reads.count(0x2100) == 2
print('Checked 256 externally changed hardware rereads')
