"""Execute branch-local index constraints and delayed cursor updates."""
import importlib.util
from pathlib import Path
import random
import subprocess
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
llc, source, directory, *entries = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)
checks = 0
for allocator in ('z80', 'greedy'):
    for banks in ('-alternate-regs', '+alternate-regs'):
        assembly = subprocess.check_output([
            llc, '-mtriple=z80', '-mattr='+banks, '-regalloc='+allocator,
            '-start-before=z80-machine-prera-opt', '-verify-machineinstrs',
            source, '-o', '-'], text=True)
        (directory/(allocator+banks+'.s')).write_text(assembly)
        program = x.Program(assembly)
        for entry in entries:
            for seed in range(128):
                for base in (0, 0xffff, 0xfffe, 0x4000+seed):
                    for choice in (0, 1):
                        cpu = x.CPU(random.Random(seed))
                        cpu.put('de', base)
                        cpu.put('a', choice)
                        if entry == 'indexed_branch':
                            offsets = (2,) if choice else (0, 1, 2, 3)
                        else:
                            offsets = (2, 1) if entry == 'changed_base' else (0, 1)
                        expected = sum(cpu.mem[(base+i)&65535] for i in offsets)&255
                        sp, ix = cpu.sp, cpu.get('ix')
                        cpu.run(program, entry=entry)
                        assert cpu.get('a') == expected, (allocator,banks,entry,seed,base,choice)
                        assert cpu.sp == sp+2 and cpu.get('ix') == ix
                        checks += 1
print(f'{checks} cursor placement executions passed')
