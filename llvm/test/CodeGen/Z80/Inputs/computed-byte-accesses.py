"""Execute computed neighboring addresses, including a later use of the base."""
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
            for direction, reuse in ((1, False), (-1, False), (1, True), (-1, True)):
                text = Path(source).read_text().replace('ptr %p, i16 1', f'ptr %p, i16 {direction}')
                if size:
                    text = text.replace('nounwind {', 'nounwind minsize optsize {')
                if reuse:
                    text = text.replace('  ret i8 %b', '''  store volatile i8 %b, ptr inttoptr (i16 8961 to ptr)
  %again = load volatile i8, ptr %p
  %result = xor i8 %again, %b
  ret i8 %result''')
                ir = directory / f'{allocator}-{bank}-{size}-{direction}-{reuse}.ll'
                ir.write_text(text)
                assembly = subprocess.check_output([
                    llc, '-regalloc='+allocator,
                    '-mattr='+('+' if bank else '-')+'alternate-regs',
                    '-verify-machineinstrs', str(ir), '-o', '-'], text=True)
                for base in (32768, 65535):
                    program = x.Program(assembly.replace('table', str(base)))
                    for index in range(256):
                        cpu = x.CPU(random.Random(index))
                        cpu.mem[8192] = index
                        initial = cpu.get('c')
                        address = (base + index*2) & 65535
                        first = cpu.mem[address]
                        a = (first + initial) & 255
                        b = (a + cpu.mem[(address+direction) & 65535]) & 255
                        cpu.put('de', 8192)
                        sp, ix = cpu.sp, cpu.get('ix')
                        cpu.run(program, entry='lookup')
                        assert cpu.get('a') == (b ^ first if reuse else b), ir
                        assert cpu.mem[8960] == a, ir
                        if reuse:
                            assert cpu.mem[8961] == b, ir
                        assert cpu.sp == sp + 2 and cpu.get('ix') == ix, ir
                        checks += 1
print(f'{checks} computed-address executions passed')
