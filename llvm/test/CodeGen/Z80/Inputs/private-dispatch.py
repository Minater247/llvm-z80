"""Check cursor caching across a dispatcher and its observing calls."""
import importlib.util
from pathlib import Path
import random
import re
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
        for cached in (False, True):
            flags = [llc, '-regalloc='+allocator,
                     '-mattr='+('+' if bank else '-')+'alternate-regs',
                     '-z80-cache-private-memory='+str(cached).lower(),
                     '-verify-machineinstrs', source]
            prepared = directory / f'{allocator}-{bank}-{cached}.mir'
            subprocess.run([*flags, '-stop-before=irtranslator', '-o', str(prepared)], check=True)
            text = prepared.read_text()
            assert ('private.slot' in text) == cached
            if cached:
                # The three observers share one publication. Their intervening
                # quiet calls leave the global and the cached value unchanged.
                observers = text.split('  observers:', 1)[1].split('  join:', 1)[0]
                assert len(re.findall(r'store i8 .*ptr @cursor', observers)) == 1
                entry = text.split('  define i8 @run', 1)[1].split('  begin:', 1)[0]
                assert not re.search(r'load i8, ptr @cursor', entry)
            assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
            assembly = re.sub(r'\bcursor\b', '8192', assembly)
            assembly += '''
observe:
ld a, (8192)
ld (8193), a
add a, 7
ld (8192), a
ret
'''
            program = x.Program(assembly)
            for start in range(0, 256, 7):
                for count in (0, 1, 7, 8, 17, 255):
                    cpu = x.CPU(random.Random(start))
                    cpu.mem[8192], cpu.mem[8193] = 123, 77
                    cpu.put('e', start)
                    cpu.put('d', count)
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry='run')
                    expected = (start + count + 21 * (count // 8) + 7) & 255
                    assert (cpu.get('a'), cpu.mem[8192], cpu.mem[8193]) == (
                        42, expected, (expected - 7) & 255), (allocator, bank, cached, start, count)
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    checks += 1
print(f'Checked {checks} private-dispatch executions')
