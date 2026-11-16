"""Check scalar load routing, live accumulators and word assembly."""
import importlib.util
from pathlib import Path
import random
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
llc, source = sys.argv[1:]
checks = 0
for allocator in ('z80', 'greedy'):
    for banks in ('-alternate-regs', '+alternate-regs'):
        assembly = subprocess.check_output([
            llc, '-start-before=z80-machine-prera-opt', '-regalloc='+allocator,
            '-mattr='+banks, '-verify-machineinstrs', source, '-o', '-'], text=True)
        program = x.Program(assembly)
        for seed in range(32):
            for base in (0, 0xffff, 0xfffe, 0x30fe):
                for entry in ('scalar_stream', 'live_accumulator', 'word_assembly'):
                    cpu = x.CPU(random.Random(seed))
                    data = [cpu.mem[(base+i)&65535] for i in range(4)]
                    cpu.put('de', base)
                    cpu.put('bc', 0x4000)
                    cpu.put('a', 17)
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry=entry)
                    context = (allocator, banks, seed, base, entry)
                    if entry == 'scalar_stream':
                        assert cpu.mem[0x4000] == data[2], context
                    elif entry == 'live_accumulator':
                        assert cpu.get('a') == (17+sum(data[:3]))&255, context
                    else:
                        assert cpu.get('de') == data[0]+256*data[1], context
                        assert cpu.get('hl') == data[2]+256*data[3], context
                    assert cpu.sp == sp+2 and cpu.get('ix') == ix, context
                    checks += 1
print(f'{checks} scalar stream executions passed')
