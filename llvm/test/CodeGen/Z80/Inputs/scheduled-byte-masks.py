"""Check carry masks and simultaneous ABI inputs under instruction scheduling."""
import importlib.util
from pathlib import Path
import random
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)
checks = 0
for scheduled in (False, True):
    for bank in (False, True):
        flags = [llc, '-regalloc=z80', '-verify-machineinstrs',
                 '-enable-misched='+str(scheduled).lower(),
                 '-mattr='+('+' if bank else '-')+'alternate-regs', source]
        assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
        (directory / f'{scheduled}-{bank}.s').write_text(assembly)
        program = x.Program(assembly)
        for value in range(256):
            for name in ('less_mask', 'greater_mask', 'sign_mask', 'extended_bit',
                         'input_pressure'):
                cpu = x.CPU(random.Random(value))
                other = (value * 73 + 19) & 255
                cpu.put('e', value)
                cpu.put('d', other)
                cpu.put('c', value ^ 0x5a)
                cpu.put('b', value ^ 0xa5)
                expected = bytearray(cpu.mem)
                if name == 'input_pressure':
                    address = 0x2200 + value * 8
                    if other >= expected[address]:
                        expected[address+2] = value ^ 0x5a
                        expected[address+3] = value ^ 0xa5
                result = {'less_mask': 255 if value < 160 else other,
                          'greater_mask': 255 if value >= 160 else other,
                          'sign_mask': 255 if value & 128 else 0,
                          'extended_bit': 255 if value & 1 else 0}
                ix = cpu.get('ix')
                cpu.run(program, entry=name)
                if name != 'input_pressure':
                    assert cpu.get('a') == result[name], (scheduled, bank, name, value)
                assert cpu.mem[:0xef00] == expected[:0xef00]
                assert cpu.mem[0xf002:] == expected[0xf002:]
                assert cpu.get('ix') == ix and cpu.sp == 0xf002
                checks += 1
print(f'{checks} scheduled and unscheduled byte-mask/input executions passed')
