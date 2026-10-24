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
print('Checked 512 memory updates and returned results')
