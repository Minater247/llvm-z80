"""Check bounded loop shifts, conditional uses, truncation and wrapping."""
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
        for base in (1, 3, 32768):
            ir = directory / f'{allocator}-{bank}-{base}.ll'
            text = Path(source).read_text().replace('shl i16 1,', f'shl i16 {base},')
            ir.write_text(text)
            for enabled in (False, True):
                flags = [llc, '-regalloc='+allocator,
                         '-mattr='+('+' if bank else '-')+'alternate-regs',
                         '-z80-prepare-loop-masks='+str(enabled).lower(),
                         '-verify-machineinstrs', str(ir)]
                prepared = ir.with_suffix(f'.{enabled}.mir')
                subprocess.run([*flags, '-stop-before=irtranslator', '-o', str(prepared)], check=True)
                assert ('%loop.mask = phi' in prepared.read_text()) == enabled
                assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
                program = x.Program(assembly)
                for count in range(32):
                    cpu = x.CPU(random.Random(count))
                    cpu.put('e', count)
                    before = bytes(cpu.mem[16384:16392])
                    sp, ix = cpu.sp, cpu.get('ix')
                    cpu.run(program, entry='run')
                    used = range(1, count & 7, 2)
                    expected = sum((base << i) & 65535 for i in used) & 65535
                    assert cpu.get('hl') == expected, (allocator, bank, base, enabled, count)
                    for i in range(8):
                        value = (base << i) & 255 if i in used else before[i]
                        assert cpu.mem[16384+i] == value
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    checks += 1

# An unbounded count can wrap its induction variable and revisit small shift
# counts. Preserve its original expression without a proven unsigned bound.
unbounded = directory / 'unbounded.ll'
unbounded.write_text(Path(source).read_text().replace(
    '%limit = and i8 %count, 7', '%limit = add i8 %count, 0').replace(
    '%next = add nuw i8 %i, 1', '%next = add i8 %i, 1'))
text = subprocess.check_output([llc, '-stop-before=irtranslator',
                                str(unbounded), '-o', '-'], text=True)
assert '%loop.mask = phi' not in text
short = directory / 'short.ll'
short.write_text(Path(source).read_text().replace(
    '%limit = and i8 %count, 7', '%limit = and i8 %count, 1'))
text = subprocess.check_output([llc, '-stop-before=irtranslator',
                                str(short), '-o', '-'], text=True)
assert '%loop.mask = phi' not in text
print(f'Checked {checks} bounded-mask executions')
