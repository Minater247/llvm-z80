"""Check byte spill packing, pair aliases and call-clobbered bank storage."""
import importlib.util
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text() + '''
external:
  exx
  ld hl, 1234
  ld de, 5678
  ld bc, 9012
  exx
  ex af, af'
  ld a, 42
  ex af, af'
  ld a, 99
  ret
''')
rng = random.Random(3817)
for function in ('two_bytes', 'word_bytes', 'fixed_lane', 'partial_neighbor',
                 'call_live'):
    for case in range(128):
        cpu = execution.CPU(rng)
        cpu.sp = 0xf000
        initial, alternate = dict(cpu.r), dict(cpu.alt)
        expected = initial['a']
        if function == 'two_bytes':
            expected = (initial['h'] + initial['l']) & 255
        elif function == 'word_bytes':
            expected = (initial['a'] + initial['c']) & 255
        elif function == 'partial_neighbor':
            expected = alternate['h']
        cpu.run(program, entry=function)
        assert cpu.get('a') == expected, (function, case, cpu.get('a'), expected)
        assert cpu.sp == 0xf002, (function, case, 'SP')
        assert (cpu.r['ixh'], cpu.r['ixl']) == (initial['ixh'], initial['ixl'])
        assert cpu.writes <= set(range(0xef80, 0xf000)), (function, case, 'writes')
        if function == 'word_bytes':
            assert cpu.get('de') == (initial['h'] << 8) | initial['l']
        elif function == 'partial_neighbor':
            assert cpu.alt['h'] == alternate['h']
            word = (alternate['d'] << 8) | alternate['e']
            assert (cpu.alt['d'] << 8) | cpu.alt['e'] == (word + 1) & 65535
        elif function == 'fixed_lane':
            assert cpu.alt['h'] == alternate['h']
print('Checked 640 byte-bank spill executions')
