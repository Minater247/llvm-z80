"""Exercise fused reads across primary pairs, byte destinations and live flags."""
import importlib.util
from pathlib import Path
import random
import sys

cases = [('bc', 'a'), ('de', 'a'), ('hl', 'a'), ('hl', 'c'),
         ('iy', 'a'), ('iy', 'e'), ('hl', 'iyh'), ('hl', 'iyl')]
if sys.argv[1] == '--emit':
    names = [(p, d, f'read_{p}_{d}') for p, d in cases]
    text = '--- |\n  target triple = "z80"\n'
    text += ''.join(f'  define void @{name}() {{ ret void }}\n' for _, _, name in names)
    text += '...\n'
    for pair, dst, name in names:
        opcode = 'LD8A_POST16' if dst == 'a' else 'LD8_POST16'
        text += (f'---\nname: {name}\ntracksRegLiveness: true\nbody: |\n'
                 f'  bb.0:\n    liveins: ${pair}, $a, $f\n'
                 f'    early-clobber ${dst}, ${pair} = {opcode} ${pair} :: (volatile load (s8))\n'
                 f'    RET16 implicit-def $sps, implicit $sps, implicit ${pair}, implicit ${dst}, implicit $a, implicit $f\n...\n')
    Path(sys.argv[2]).write_text(text)
    sys.exit()

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(1651)
checks = 0
for ptr in [0, 1, 255, 256, 32767, 65534, 65535] + [rng.randrange(0x100, 0xe000) for _ in range(256)]:
    for pair, dst in cases:
        cpu = execution.CPU(rng)
        cpu.put(pair, ptr)
        expected = cpu.mem[ptr]
        before = {r: cpu.get(r) for r in ('a', 'f', 'bc', 'de', 'hl', 'ix', 'iy')}
        sp = cpu.sp
        cpu.run(program, entry=f'read_{pair}_{dst}')
        assert cpu.get(pair) == (ptr + 1) & 65535
        assert cpu.get(dst) == expected
        assert cpu.get('f') == before['f']
        if dst != 'a':
            assert cpu.get('a') == before['a']
        assert cpu.sp == sp + 2 and cpu.get('ix') == before['ix']
        for reg in ('bc', 'de', 'hl', 'iy'):
            if reg != pair and dst not in cpu.pair(reg):
                assert cpu.get(reg) == before[reg], (pair, dst, reg)
        assert cpu.reads.count(ptr) == 1
        assert cpu.writes <= set(range(sp - 8, sp))
        checks += 1
print(f'{checks} post-increment executions passed')
