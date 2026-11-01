"""Check stack-pointer copies, partial scratch lanes and live condition flags."""
import importlib.util
from pathlib import Path
import random
import sys

bits = int(sys.argv[2])
pairs = ['bc', 'de', 'hl', 'ix', 'iy']
lanes = [[], ['l'], ['h'], ['l', 'h']]
if bits == 24:
    lanes += [['hlu'], ['l', 'h', 'hlu']]
cases = [(p, keep, flags, f'sp_{p}_{i}_{flags}')
         for p in pairs for i, keep in enumerate(lanes) for flags in (0, 1)]

def reg(p):
    return ('u' if bits == 24 and p in pairs else '') + p

if sys.argv[1] == '--emit':
    text = f'--- |\n  target triple = "{"ez80" if bits == 24 else "z80"}"\n'
    text += ''.join(f'  define void @{name}() {{ ret void }}\n' for _, _, _, name in cases)
    text += '...\n'
    sp = 'spl' if bits == 24 else 'sps'
    for dst, keep, flags, name in cases:
        live = ['a'] + (keep if dst != 'hl' else []) + (['f'] if flags else [])
        uses = ', '.join('implicit $' + p for p in [reg(dst)] + live)
        text += (f'---\nname: {name}\ntracksRegLiveness: true\nbody: |\n'
                 f'  bb.0:\n    liveins: {", ".join("$"+p for p in live)}\n'
                 f'    ${reg(dst)} = COPY ${sp}\n'
                 f'    RET{bits} implicit-def ${sp}, implicit ${sp}, {uses}\n...\n')
    Path(sys.argv[3]).write_text(text)
    sys.exit()

def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name+'.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

z80 = module('z80-execution')
adl = module('ez80-execution')
assembly = Path(sys.argv[3]).read_text()
program = z80.Program(assembly)
rng = random.Random(8041)
checks = 0
for dst, keep, flags, name in cases:
    start = program.labels[name]
    end = next(i for i in range(start, len(program.code)) if program.code[i][0] == 'ret')
    saves = [args for op, args in program.code[start:end] if op == 'push' and args == ['af']]
    assert bool(saves) == bool(flags), (name, saves)
    for f in range(256):
        cpu = adl.CPU(rng) if bits == 24 else z80.CPU(rng)
        state = cpu.reg if bits == 24 else cpu.r
        state['f'] = f
        sp = rng.randrange(16, (1 << bits) - 16)
        cpu.sp = sp
        before = state.copy()
        width = bits // 8
        if bits == 24:
            cpu.store(sp, 0x654321, width)
            cpu.writes.clear()
            cpu.run(program, name)
        else:
            cpu.mem[sp:sp+2] = b'\x34\x12'
            cpu.run(program, entry=name)
            assert cpu.mem[sp:sp+2] == b'\x34\x12'
        assert cpu.get(dst) == sp, (name, sp, cpu.get(dst))
        assert cpu.sp == sp + width
        assert cpu.get('a') == before['a']
        if flags:
            assert cpu.get('f') == before['f'], (name, f, cpu.get('f'))
        if dst != 'hl':
            for lane in keep:
                assert cpu.get(lane) == before[lane], (name, lane)
        for p in pairs:
            if p not in (dst, 'hl'):
                for lane in (adl.PAIRS[p] if bits == 24 else cpu.pair(p)):
                    assert cpu.get(lane) == before[lane], (name, lane)
        assert cpu.writes <= set(range(sp - 2*width, sp)), (name, cpu.writes)
        checks += 1
print(f'{checks} {bits}-bit stack-pointer copies passed')
