"""Check cursor values across page boundaries, delayed loads and real calls."""
import importlib.util
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
assembly, function = sys.argv[1:]
program = execution.Program(Path(assembly).read_text() + '''
external:
  ld a, e
  inc a
  ld l, a
  ld h, 143
  ld bc, 12345
  ld de, 6789
  ld iy, 23456
  ret
''')
rng = random.Random(36119)
for case in range(128):
    cpu = execution.CPU(rng)
    cpu.mem[:] = rng.randbytes(65536)
    cpu.sp = 0xf000
    initial, alternate = dict(cpu.r), dict(cpu.alt)
    page = rng.randrange(0x30, 0x50)
    if function == 'table_sum':
        source = (0x21ff, 0xfffe, 0xffff, rng.randrange(0x2000, 0x3000))[case % 4]
        cpu.put('de', source)
        cpu.put('c', page)
        expected = sum(cpu.mem[page * 256 + cpu.mem[(source+i) & 65535]]
                       for i in range(3)) & 255
        calls = 0
    else:
        assert function in ('cursor_tail', 'live_out_pair')
        calls = case % 8 + 1
        cpu.put('de', page * 256 + 256 - calls)
        expected = cpu.mem[page * 256 + 1]
    cpu.run(program, entry=function)
    assert cpu.get('a') == expected, (function, case, cpu.get('a'), expected)
    assert cpu.calls == calls and cpu.sp == 0xf002, (function, case, 'calls/SP')
    assert (cpu.r['ixh'], cpu.r['ixl']) == (initial['ixh'], initial['ixl'])
    assert cpu.writes <= set(range(0xef80, 0xf000)), (function, case, 'writes')
    if function in ('cursor_tail', 'live_out_pair'):
        assert cpu.alt == alternate
print('Checked 128', function, 'executions')
