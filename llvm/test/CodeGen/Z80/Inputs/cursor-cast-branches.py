"""Check cursor casts across branches with full-width carry and borrow."""
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
            text = Path(source).read_text()
            if direction < 0:
                for step in (1, 2, 3):
                    text = text.replace(f', {step}\n', f', -{step}\n')
                    text = text.replace(f', i16 {step}\n', f', i16 -{step}\n')
            ir = directory / f'{allocator}-{bank}-{direction}.ll'
            ir.write_text(text)
            assembly = subprocess.check_output([
                llc, '-verify-machineinstrs', '-regalloc='+allocator,
                '-mattr='+('+' if bank else '-')+'alternate-regs',
                str(ir), '-o', '-'], text=True)
            program = x.Program(assembly + "\nopaque:\nret\n")
            for name in ('advance', 'page_advance'):
                for page in (0, 1, 127, 255):
                    for low in range(256):
                        for choice in (0, 1):
                            cpu = x.CPU(random.Random(low))
                            base = page*256+low
                            cpu.put('de', base)
                            cpu.put('c', choice)
                            before = bytes(cpu.mem)
                            sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                            def address(step):
                                value = base+direction*step
                                return (0x3100 | (value & 255) if name == 'page_advance'
                                        else value & 65535)
                            expected = bytearray(before)
                            destinations = {address(1)}
                            expected[address(1)] = 17
                            if not choice:
                                expected[address(2)] = 34
                                destinations.add(address(2))
                            cpu.run(program, entry=name)
                            assert cpu.get('hl') == address(1 if choice else 3)
                            assert cpu.mem[:sp-128] == expected[:sp-128]
                            assert cpu.mem[sp+2:] == expected[sp+2:]
                            assert cpu.writes <= destinations | set(range(sp-128,sp))
                            assert cpu.sp == sp+2 and cpu.get('ix') == ix
                            if not bank:
                                assert cpu.alt == alternate
                            checks += 1
print(f'{checks} cursor branch executions passed')
