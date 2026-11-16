"""Execute neighboring ALU operands with wrapping and aliased destinations."""
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
operations = {
    'add': lambda a, b: a + b,
    'sub': lambda a, b: a - b,
    'and': lambda a, b: a & b,
    'or': lambda a, b: a | b,
    'xor': lambda a, b: a ^ b,
}
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        for direction in (1, -1):
            for op, operation in operations.items():
                text = Path(source).read_text().replace(
                    'ptr %p, i16 1', f'ptr %p, i16 {direction}')
                text = text.replace('%value = add i8 %hi, %a',
                                    f'%value = {op} i8 %a, %hi')
                ir = directory / f'{allocator}-{bank}-{direction}-{op}.ll'
                ir.write_text(text)
                assembly = subprocess.check_output([
                    llc, '-regalloc='+allocator,
                    '-mattr='+('+' if bank else '-')+'alternate-regs',
                    '-verify-machineinstrs', str(ir), '-o', '-'], text=True)
                program = x.Program(assembly)
                for base in (8192, 8447, 8960, 8961, 0, 65535):
                    for seed in range(64):
                        cpu = x.CPU(random.Random(seed))
                        initial = (seed * 37) & 255
                        cpu.put('de', base)
                        cpu.put('c', initial)
                        expected = bytearray(cpu.mem)
                        a = (expected[base] + initial) & 255
                        expected[8960] = a
                        value = operation(a, expected[(base+direction)&65535]) & 255
                        expected[8961] = value
                        result = value ^ expected[base]
                        sp, ix = cpu.sp, cpu.get('ix')
                        alternate = dict(cpu.alt)
                        cpu.run(program, entry='run')
                        context = (ir, base, seed)
                        assert cpu.get('a') == result, context
                        assert cpu.mem[8960:8962] == expected[8960:8962], context
                        assert cpu.sp == sp + 2 and cpu.get('ix') == ix, context
                        if not bank:
                            assert cpu.alt == alternate, context
                        checks += 1
                if op != 'add':
                    continue
                for entry in ('ordered', 'store_boundary', 'shared_load'):
                    for base in (8447, 8960, 8961, 65535):
                        for seed in range(64):
                            cpu = x.CPU(random.Random(seed))
                            initial = seed * 3
                            cpu.put('de', base)
                            cpu.put('c', initial)
                            expected = bytearray(cpu.mem)
                            q = (base + direction) & 65535
                            a, b = expected[base], expected[q]
                            if entry == 'store_boundary':
                                expected[q] = initial
                            elif entry == 'shared_load':
                                expected[8960] = b
                            sp, ix = cpu.sp, cpu.get('ix')
                            cpu.run(program, entry=entry)
                            context = (ir, entry, base, seed)
                            assert cpu.get('a') == (a+b+initial)&255, context
                            assert cpu.mem[q] == expected[q], context
                            assert cpu.mem[8960] == expected[8960], context
                            assert cpu.sp == sp+2 and cpu.get('ix') == ix, context
                            checks += 1
print(f'{checks} adjacent-ALU executions passed')
