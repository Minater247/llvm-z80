"""Check record stores and live byte values after joint pair reassignment."""
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
for bank in (False, True):
    assembly = subprocess.check_output([
        llc, '-regalloc=z80', '-verify-machineinstrs',
        '-mattr='+('+' if bank else '-')+'alternate-regs', source, '-o', '-'],
        text=True)
    (directory/f'record-{bank}.s').write_text(assembly)
    program = x.Program(assembly)
    for index in range(16):
        for seed in range(32):
            rng = random.Random(index*32+seed)
            cpu = x.CPU(rng)
            cpu.put('e', index)
            values = bytes(rng.randrange(256) for _ in range(4))
            flags = bytes(rng.randrange(256) for _ in range(2))
            cpu.mem[0x3128:0x312c] = values
            cpu.mem[0x3010:0x3012] = flags
            address = 300+4*index
            before = cpu.mem[address-1:address+5]
            sp, ix = cpu.sp, cpu.get('ix')
            cpu.run(program, entry='record')
            assert cpu.mem[address:address+4] == values, (bank, index, seed)
            assert cpu.mem[address-1] == before[0]
            assert cpu.mem[address+4] == before[5]
            mask = (1 << index)^255 if index < 8 else 255
            assert cpu.mem[0x3010:0x3012] == bytes(v & mask for v in flags)
            assert cpu.sp == sp+2 and cpu.get('ix') == ix
            checks += 1
print(f'{checks} joint placement executions passed')
