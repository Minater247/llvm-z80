"""Check sums stored through independent, overlapping and wrapping cursors."""
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
        for size in (False, True):
            text = Path(source).read_text()
            if size:
                text = text.replace('nounwind {', 'nounwind minsize optsize {')
            ir = directory / f'{allocator}-{bank}-{size}.ll'
            ir.write_text(text)
            assembly = subprocess.check_output([
                llc, '-regalloc='+allocator,
                '-mattr='+('+' if bank else '-')+'alternate-regs',
                '-verify-machineinstrs', str(ir), '-o', '-'], text=True)
            program = x.Program(assembly)
            for start in range(256):
                source_address = 8192 + start
                for output in (8960 + start, source_address, source_address+1):
                    cpu = x.CPU(random.Random(start))
                    expected = bytearray(cpu.mem)
                    a = (expected[source_address] + expected[source_address+1]) & 255
                    expected[output] = a
                    b = (a + expected[source_address+1]) & 255
                    dest = (output & 0xff00) | ((output+1) & 255)
                    expected[dest] = b
                    cpu.put('de', source_address)
                    cpu.put('bc', output)
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry='run')
                    assert cpu.get('a') == b, (ir, start, output)
                    assert cpu.mem[output] == expected[output]
                    assert cpu.mem[dest] == expected[dest]
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    checks += 1
print(f'{checks} accumulator-store executions passed')
