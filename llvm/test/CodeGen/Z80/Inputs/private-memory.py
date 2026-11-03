"""Check private byte caching, observer synchronization, wrapping and exits."""
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
        for variant in ('cached', 'disabled', 'volatile', 'escaped', 'barrier'):
            ir = directory / f'{allocator}-{bank}-{variant}.ll'
            text = Path(source).read_text()
            if variant == 'volatile':
                text = text.replace('load i8, ptr @cursor',
                                    'load volatile i8, ptr @cursor')
            if variant == 'barrier':
                text = text.replace('  ret void\n}', '  fence seq_cst\n  ret void\n}', 1)
            if variant == 'escaped':
                text += '\n@escape = global ptr @cursor\n'
            ir.write_text(text)
            out = ir.with_suffix('.s')
            flags = [llc, '-regalloc='+allocator,
                     '-mattr='+('+' if bank else '-')+'alternate-regs',
                     '-z80-cache-private-memory='+str(variant != 'disabled').lower(),
                     '-verify-machineinstrs', str(ir)]
            prepared = ir.with_suffix('.mir')
            subprocess.run([*flags, '-stop-before=irtranslator', '-o', str(prepared)], check=True)
            assert ('private.slot' in prepared.read_text()) == (variant == 'cached')
            if variant == 'barrier':
                # Fences are checked before instruction selection; this target
                # does not lower them to executable code yet.
                continue
            subprocess.run([*flags, '-o', str(out)], check=True)
            assembly = re.sub(r'\bcursor\b', '8192', out.read_text())
            assembly += '''
observe:
ld a, (8192)
ld (8193), a
add a, 7
ld (8192), a
ret
'''
            program = x.Program(assembly)
            for start in range(256):
                for count in (0, 1, 3, 17):
                    cpu = x.CPU(random.Random(start))
                    cpu.mem[8192], cpu.mem[8193] = 123, 77
                    cpu.put('e', start)
                    cpu.put('d', count)
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry='run')
                    result = (start + 11 * count) & 255
                    observed = (result - 7) & 255 if count else 77
                    assert (cpu.get('a'), cpu.mem[8192], cpu.mem[8193]) == (
                        result, result, observed), (allocator, bank, variant, start, count)
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    checks += 1
print(f'{checks} private-memory executions passed')
