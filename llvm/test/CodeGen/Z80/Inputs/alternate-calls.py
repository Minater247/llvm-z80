"""Execute spills across known, nested, replaceable and recursive calls."""
import importlib.util
import random
import re
import subprocess
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
llc, source, work = sys.argv[1:]
work = Path(work)
work.mkdir(exist_ok=True)
count = 0
for allocator in ('greedy', 'z80'):
    for bank in (False, True):
        for ipra in (False, True):
            path = work / f'{allocator}-{bank}-{ipra}.s'
            subprocess.run([llc, '-verify-machineinstrs', '-regalloc='+allocator,
                            '-mattr='+('+' if bank else '-')+'alternate-regs',
                            '-enable-ipra='+str(ipra).lower(), source,
                            '-o', str(path)], check=True)
            assembly = path.read_text()
            # Replace the weak definition with an ABI-compatible implementation
            # that destroys all available volatile registers, including banks.
            stub = '''ld a, (8448)
add a, 14
ld (8448), a
ld hl, 1234
ld de, 5678
ld bc, 9012
ld iy, 3456
'''
            if bank:
                stub += '''exx
ld hl, 9876
ld de, 5432
ld bc, 1098
exx
ex af, af'
ld a, 255
ex af, af'
'''
            stub += 'ret\n'
            assembly = re.sub(r'(?ms)^replaceable:.*?(?=^\s*\.size\s+replaceable,)',
                              'replaceable:\n'+stub, assembly)
            program = execution.Program(assembly+'\nexternal:\n'+stub)
            rng = random.Random(247)
            cycles = {}
            for name in ('known', 'unknown', 'transitive', 'interposable', 'recursive'):
                for i in range(64):
                    cpu = execution.CPU(rng)
                    saved = bytes(cpu.mem[8192:8204])
                    sp, ix, alt = cpu.sp, cpu.get('ix'), cpu.alt.copy()
                    value = cpu.mem[8448]
                    cpu.mem[8449] = i % 8
                    cpu.run(program, entry=name)
                    assert bytes(cpu.mem[8224:8236]) == saved, (path, name, i)
                    assert cpu.mem[8448] == (value + (0 if name == 'recursive' else 14)) & 255
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    if name == 'recursive':
                        assert cpu.mem[8449] == 0
                    if not bank:
                        assert cpu.alt == alt
                    cycles[name] = cpu.cycles
                    count += 1
            print(path.stem, cycles)
print('Checked', count, 'call executions with randomized registers and memory')
