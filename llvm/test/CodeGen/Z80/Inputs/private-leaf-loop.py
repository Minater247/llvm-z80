"""Check private leaf-loop cursors, wrapping and volatile/escaping exclusions."""
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
        for variant in ('cached', 'disabled', 'volatile', 'escaped'):
            ir = directory / f'{allocator}-{bank}-{variant}.ll'
            text = Path(source).read_text()
            if variant == 'volatile':
                text = text.replace('load i8, ptr @cursor', 'load volatile i8, ptr @cursor')
            if variant == 'escaped':
                text += '\n@escape = global ptr @cursor\n'
            ir.write_text(text)
            flags = [llc, '-regalloc='+allocator,
                     '-mattr='+('+' if bank else '-')+'alternate-regs',
                     '-z80-cache-private-leaf-loops='+str(variant != 'disabled').lower(),
                     '-verify-machineinstrs', str(ir)]
            prepared = ir.with_suffix('.mir')
            subprocess.run([*flags, '-stop-before=irtranslator', '-o', str(prepared)], check=True)
            assert ('private.slot' in prepared.read_text()) == (variant == 'cached')
            assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
            program = x.Program(re.sub(r'\bcursor\b', '8192', assembly))
            for start in range(0, 256, 7):
                for count in (0, 1, 2, 17):
                    cpu = x.CPU(random.Random(start))
                    cpu.mem[8192] = start
                    cpu.put('e', count)
                    ix, sp = cpu.get('ix'), cpu.sp
                    cpu.run(program, entry='run')
                    expected = (start + count) & 255
                    assert (cpu.get('a'), cpu.mem[8192], cpu.mem[16384]) == (
                        expected, expected, (expected - 1) & 255)
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    checks += 1
print(f'Checked {checks} private leaf-loop executions')
