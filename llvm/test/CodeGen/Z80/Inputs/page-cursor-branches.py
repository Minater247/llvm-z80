"""Check runtime-page cursor updates through branches and byte wraparound."""
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
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        flags = [llc, '-regalloc='+allocator, '-verify-machineinstrs',
                 '-mattr='+('+' if bank else '-')+'alternate-regs', source]
        assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
        (directory / f'{allocator}-{bank}.s').write_text(assembly)
        program = x.Program(assembly)
        for page in (0, 0x1200, 0x8000, 0xff00):
            for low in range(256):
                cpu = x.CPU(random.Random(low))
                start = page | low
                first, second = page | ((low+1)&255), page | ((low+2)&255)
                cpu.put('de', start)
                cpu.put('c', low)
                expected = bytearray(cpu.mem)
                expected[start], expected[first] = 7, 9
                expected[first if low & 1 else start] = 13 if low & 1 else 11
                expected[second] = 17
                ix = cpu.get('ix')
                cpu.run(program, entry='advance')
                assert cpu.mem[:0xef00] == expected[:0xef00]
                assert cpu.mem[0xf002:] == expected[0xf002:]
                assert cpu.get('hl') == second
                assert cpu.get('ix') == ix and cpu.sp == 0xf002
                checks += 1
print(f'{checks} cross-block page-cursor executions passed')
