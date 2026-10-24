import importlib.util
import random
import re
import sys
from pathlib import Path
spec = importlib.util.spec_from_file_location('execute', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
asm = Path(sys.argv[1]).read_text()
for name in ['shift_left', 'shift_right', 'shift_signed']:
    body = re.search(r'^'+name+r':.*?^\s*\.size', asm, re.M | re.S)[0]
    program = x.Program(body)
    cpu = x.CPU(random.Random(247))
    initial, alternate = dict(cpu.r), dict(cpu.alt)
    for value in range(256):
        for count in range(8):
            cpu.r, cpu.alt = dict(initial), dict(alternate)
            cpu.put('e', value)
            cpu.put('d', count)
            cpu.sp = 0xf000
            cpu.writes.clear()
            cpu.cycles = 0
            cpu.run(program)
            signed = value if value < 128 else value-256
            expected = (value << count if name == 'shift_left' else signed >> count if name == 'shift_signed' else value >> count) & 255
            assert cpu.get('a') == expected, (name,value,count,cpu.get('a'),expected)
            assert cpu.sp == 0xf002 and cpu.get('ix') == initial['ixh']*256+initial['ixl']
            assert cpu.alt == alternate and cpu.writes <= set(range(0xef00,0xf000))
print('Checked all 6144 defined byte shifts')
