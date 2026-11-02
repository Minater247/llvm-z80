"""Check masked shifts for all input bytes, including shared shift results."""
import importlib.util
from pathlib import Path
import random
import sys

cases = [(op, n, mask) for op in ('lshr', 'shl') for n in range(1, 8)
         for mask in (120, 15, 128, 85, 170, 255)]
if sys.argv[1] == '--emit':
    ir = 'target triple = "z80"\n'
    for op, n, mask in cases:
        ir += (f'define i8 @{op}_{n}_{mask}(i8 %x) {{\n'
               f'  %s = {op} i8 %x, {n}\n  %r = and i8 %s, {mask}\n'
               '  ret i8 %r\n}\n')
    ir += """
define i8 @overlap(i8 %x, ptr %out) {
  %s = lshr i8 %x, 3
  store volatile i8 %s, ptr %out
  %t = lshr i8 %x, 2
  %r = and i8 %t, 1
  ret i8 %r
}
define i8 @shared(i8 %x, ptr %out) {
  %s = lshr i8 %x, 1
  store volatile i8 %s, ptr %out
  %m = and i8 %s, 120
  %r = xor i8 %m, %x
  ret i8 %r
}
"""
    Path(sys.argv[2]).write_text(ir)
    sys.exit()

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(817)
for op, n, mask in cases:
    for x in range(256):
        cpu = execution.CPU(rng)
        cpu.put('e', x)
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry=f'{op}_{n}_{mask}')
        expected = (x >> n if op == 'lshr' else x << n) & mask
        assert cpu.get('a') == expected, (op, n, mask, x, cpu.get('a'))
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 256, sp))
for x in range(256):
    cpu = execution.CPU(rng)
    cpu.put('e', x)
    cpu.put('bc', 0x6000)
    cpu.run(program, entry='shared')
    assert cpu.mem[0x6000] == x >> 1
    assert cpu.get('a') == ((x >> 1) & 120) ^ x
for x in range(256):
    cpu = execution.CPU(rng)
    cpu.put('e', x)
    cpu.put('bc', 0x6000)
    cpu.run(program, entry='overlap')
    assert cpu.mem[0x6000] == x >> 3
    assert cpu.get('a') == (x >> 2) & 1
print(f'{256 * (len(cases) + 2)} masked shift executions passed')
