"""Check adjacent full/byte cursor updates over the entire 16-bit domain."""
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
rng = random.Random(24717)
for kind, size in [('pointer', 2), ('byte', 1), ('page', 2)]:
    for direction, step in [('inc', 1), ('dec', -1)]:
        name = kind + '_' + direction
        body = re.search(r'^' + name + r':.*?^\s*\.size', assembly, re.M | re.S)[0]
        program = execution.Program(body)
        cpu = execution.CPU(rng)
        initial, alternate = dict(cpu.r), dict(cpu.alt)
        allowed = set(range(0xef80, 0xf000)) | set(range(0x2000, 0x200a))
        for value in range(65536):
            cpu.r = dict(initial)
            cpu.alt = dict(alternate)
            cpu.put('de', value)
            cpu.put('bc', 0x2000)
            cpu.sp = 0xf000
            cpu.cycles = 0
            cpu.reads.clear()
            cpu.writes.clear()
            cpu.run(program)
            mask = (1 << (size * 8)) - 1
            values = [(value + i*step) & mask for i in range(1, 5)]
            if kind == 'page':
                values = [(value & 0xff00) | (v & 255) for v in values]
            expected = b''.join(v.to_bytes(size, 'little') for v in values)
            assert cpu.mem[0x2000:0x2000+4*size] == expected, (name, value)
            assert int.from_bytes(cpu.mem[0x2008:0x200a], 'little') == value, (name, 'base', value)
            assert cpu.sp == 0xf002 and cpu.alt == alternate, (name, 'ABI')
            assert (cpu.r['ixh'], cpu.r['ixl']) == (initial['ixh'], initial['ixl'])
            assert cpu.writes <= allowed, (name, 'stray writes')
print('Checked 393216 four-step cursor executions')
