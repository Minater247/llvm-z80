"""Check variable block copies, empty ranges and successor PHIs."""
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
        assembly = subprocess.check_output(
            [llc, '-regalloc='+allocator,
             '-mattr='+('+' if bank else '-')+'alternate-regs',
             '-verify-machineinstrs', source, '-o', '-'], text=True)
        assert 'ldir' in assembly and 'call\tmemcpy' not in assembly
        program = x.Program(assembly)
        for count in (0, 1, 2, 7, 8, 255, 256, 257, 1023, 4096, 8191):
            for entry, condition in (('copy', 0), ('choose', 0), ('choose', 1), ('ends', 0)):
                cpu = x.CPU(random.Random(count))
                cpu.put('de', count)
                cpu.put('c', condition)
                if entry == 'ends':
                    cpu.put('de', 8192)
                    cpu.put('bc', 32768)
                    cpu.put('hl', count)
                before = bytes(cpu.mem[32767:40960])
                expected = bytes(cpu.mem[8192:8192+count])
                sp, ix = cpu.sp, cpu.get('ix')
                cpu.run(program, entry=entry)
                if not condition:
                    assert bytes(cpu.mem[32768:32768+count]) == expected
                    assert bytes(cpu.mem[32768+count:40960]) == before[count+1:]
                else:
                    assert bytes(cpu.mem[32767:40960]) == before
                assert cpu.mem[32767] == before[0]
                if entry == 'ends':
                    assert cpu.get('hl') == ((8192+count) ^ (32768+count))
                else:
                    assert cpu.get('hl' if entry == 'copy' else 'a') == (
                        count if entry == 'copy' else 41 if condition else 23)
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                checks += 1
print(f'{checks} variable-copy executions passed')
