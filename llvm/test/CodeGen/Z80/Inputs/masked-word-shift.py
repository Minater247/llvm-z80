"""Check masked word extracts across source bytes, signs and shared results."""
import importlib.util
from pathlib import Path
import random
import sys

cases = [(16, 'lshr', 6, 2), (16, 'ashr', 6, 2),
         (16, 'lshr', 7, 128), (16, 'lshr', 1, 128),
         (16, 'lshr', 8, 255), (16, 'ashr', 9, 127),
         (16, 'ashr', 15, 3), (16, 'lshr', 6, 255),
         (16, 'lshr', 3, 0xff00), (32, 'lshr', 22, 2),
         (32, 'ashr', 23, 128), (32, 'lshr', 15, 0xa0)]
if sys.argv[1] == '--emit':
    ir = 'target triple = "z80"\n'
    for width, op, count, mask in cases:
        name = f'w{width}_{op}_{count}_{mask}'
        ir += (f'define i16 @{name}(i{width} %x) {{\n'
               f'  %s = {op} i{width} %x, {count}\n'
               f'  %r = and i{width} %s, {mask}\n')
        if width == 32:
            ir += '  %t = trunc i32 %r to i16\n  ret i16 %t\n}\n'
        else:
            ir += '  ret i16 %r\n}\n'
    ir += '''
define i16 @shared(i16 %x, ptr %out) {
  %s = lshr i16 %x, 6
  store volatile i16 %s, ptr %out
  %r = and i16 %s, 2
  ret i16 %r
}
'''
    Path(sys.argv[2]).write_text(ir)
    sys.exit()

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(47102)
checks = 0
for width, op, count, mask in cases:
    values = {v << byte for byte in range(0, width, 8) for v in range(256)}
    values |= {0, (1 << width) - 1, (1 << (width - 1)) - 1, 1 << (width - 1)}
    values |= {rng.randrange(1 << width) for _ in range(512)}
    for value in sorted(values):
        cpu = execution.CPU(rng)
        cpu.put('de', value & 65535)
        if width == 32:
            cpu.put('bc', value >> 16)
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.run(program, entry=f'w{width}_{op}_{count}_{mask}')
        signed = value - (1 << width) if value & (1 << (width - 1)) else value
        expected = ((signed if op == 'ashr' else value) >> count) & mask
        assert cpu.get('hl') == expected, (width, op, count, mask, value)
        assert cpu.sp == sp + 2 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 256, sp))
        checks += 1
for value in range(0, 65536, 61):
    cpu = execution.CPU(rng)
    cpu.put('de', value)
    cpu.put('bc', 0x6000)
    cpu.run(program, entry='shared')
    assert cpu.get('hl') == (value >> 6) & 2
    assert cpu.mem[0x6000] | (cpu.mem[0x6001] << 8) == value >> 6
    checks += 1
print(f'{checks} masked word-shift executions passed')
