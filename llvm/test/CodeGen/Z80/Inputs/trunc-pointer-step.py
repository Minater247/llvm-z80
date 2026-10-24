"""Check low-byte steps and a shared wide result over all 16-bit pointers."""
import importlib.util
import random
import re
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execute', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
assembly = Path(sys.argv[1]).read_text()
rng = random.Random(24716)
for name, step in [('pointer_inc', 1), ('pointer_dec', -1), ('shared', 1),
                   ('pointer_inc2', 2), ('pointer_dec2', -2)]:
    body = re.search(r'^' + name + r':.*?^\s*\.size', assembly, re.M | re.S)[0]
    cpu = execution.CPU(rng)
    initial, alternate = dict(cpu.r), dict(cpu.alt)
    for value in range(65536):
        cpu.r = dict(initial)
        cpu.alt = dict(alternate)
        cpu.put('de', value)
        cpu.put('bc', 0x2000)
        cpu.sp = 0xf000
        cpu.cycles = 0
        cpu.reads.clear()
        cpu.writes.clear()
        cpu.run(body)
        expected = (value + step) & 65535
        assert cpu.get('a') == (expected & 255), (name, value)
        assert cpu.sp == 0xf002 and cpu.alt == alternate, (name, 'ABI')
        assert (cpu.r['ixh'], cpu.r['ixl']) == (initial['ixh'], initial['ixl'])
        allowed = set(range(0xef80, 0xf000))
        if name == 'shared':
            assert int.from_bytes(cpu.mem[0x2000:0x2002], 'little') == expected, value
            allowed.update([0x2000, 0x2001])
        assert cpu.writes <= allowed, (name, 'stray writes')
print('Checked 327680 pointer-step executions')
