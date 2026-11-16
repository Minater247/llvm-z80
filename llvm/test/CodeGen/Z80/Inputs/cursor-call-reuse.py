"""Check cursor reuse across calls that clobber ordinary argument registers."""
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
        for direction in (1, -1):
            text = Path(source).read_text().replace(
                'i16 %base, 1', f'i16 %base, {direction}').replace(
                'i16 %base, 2', f'i16 %base, {2*direction}')
            ir = directory / f'{allocator}-{bank}-{direction}.ll'
            ir.write_text(text)
            assembly = subprocess.check_output([
                llc, '-regalloc='+allocator,
                '-mattr='+('+' if bank else '-')+'alternate-regs',
                '-verify-machineinstrs', str(ir), '-o', '-'], text=True)
            for name in ('opaque', 'indexed'):
                assembly += f'\n{name}:\n ld bc,77\n ld de,101\n ld hl,103\n xor a\n ret\n'
            program = x.Program(assembly)
            for entry in ('reuse', 'index_argument'):
                for base in range(65500, 65536):
                    for choice in (0, 1):
                        cpu = x.CPU(random.Random(base))
                        expected = cpu.mem[(base+direction)&65535]
                        cpu.put('de', base)
                        cpu.put('c', choice)
                        sp, ix = cpu.sp, cpu.get('ix')
                        alternate = dict(cpu.alt)
                        cpu.run(program, entry=entry)
                        context = (ir, entry, base, choice)
                        assert cpu.get('hl') == (base+direction*(1 if choice else 2))&65535, context
                        assert cpu.mem[8960] == expected, context
                        assert cpu.sp == sp+2 and cpu.get('ix') == ix, context
                        if not bank:
                            assert cpu.alt == alternate, context
                        checks += 1
print(f'{checks} cursor-call executions passed')
