"""Exercise comparisons before and after byte extension, including bounds."""
import importlib.util
from pathlib import Path
import random
import sys

predicates = ['eq', 'ne', 'ult', 'ule', 'ugt', 'uge', 'slt', 'sle', 'sgt', 'sge']
cases = []
for ext in ('zext', 'sext'):
    for pred in predicates:
        cases.append((ext, ext, None, False, pred))
        for constant in (-129, -128, -1, 0, 127, 128, 255, 256):
            for swap in (False, True):
                cases.append((ext, None, constant, swap, pred))
for pred in predicates:
    cases.append(('zext', 'sext', None, False, pred))

def name(case):
    ext, rhs, constant, swap, pred = case
    return f'{ext}_{rhs or "c"+str(constant).replace("-", "n")}_{int(swap)}_{pred}'

if sys.argv[1] == '--emit':
    bits = int(sys.argv[2])
    text = f'--- |\n  target triple = "{"ez80" if bits == 24 else "z80"}"\n'
    text += ''.join(f'  define i8 @{name(c)}() {{ ret i8 0 }}\n' for c in cases)
    text += '...\n'
    for case in cases:
        ext, rhs, constant, swap, pred = case
        text += (f'---\nname: {name(case)}\ntracksRegLiveness: true\nbody: |\n'
                 '  bb.0:\n    liveins: $e, $d\n'
                 '    %0:_(s8) = COPY $e\n    %1:_(s8) = COPY $d\n'
                 f'    %2:_(s{bits}) = G_{ext.upper()} %0\n')
        text += (f'    %3:_(s{bits}) = G_{rhs.upper()} %1\n' if rhs else
                 f'    %3:_(s{bits}) = G_CONSTANT i{bits} {constant}\n')
        a, b = ('%3', '%2') if swap else ('%2', '%3')
        text += (f'    %4:_(s1) = G_ICMP intpred({pred}), {a}, {b}\n'
                 '    %5:_(s8) = G_ZEXT %4\n    $a = COPY %5\n'
                 f'    RET{24 if bits==24 else 16} implicit-def ${"spl" if bits==24 else "sps"}, '
                 f'implicit ${"spl" if bits==24 else "sps"}, implicit $a\n...\n')
    Path(sys.argv[3]).write_text(text)
    sys.exit()

spec = importlib.util.spec_from_file_location('execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[2]).read_text())
rng = random.Random(25616)
cpu = execution.CPU(rng)
boundaries = [0, 1, 2, 126, 127, 128, 254, 255]
checks = 0
for case in cases:
    ext, rhs, constant, swap, pred = case
    inputs = ((a, b) for a in range(256) for b in boundaries) if rhs else ((a, 0) for a in range(256))
    for a, b in inputs:
        left = a - 256 if ext == 'sext' and a >= 128 else a
        right = (b - 256 if rhs == 'sext' and b >= 128 else b) if rhs else constant
        if swap:
            left, right = right, left
        if pred.startswith('u') or pred in ('eq', 'ne'):
            left &= 65535
            right &= 65535
        condition = pred[-2:]
        expected = {'eq': left == right, 'ne': left != right,
                    'lt': left < right, 'le': left <= right,
                    'gt': left > right, 'ge': left >= right}[condition]
        cpu.sp = 0xf000
        cpu.put('e', a)
        cpu.put('d', b)
        cpu.put('f', rng.randrange(256))
        cpu.run(program, entry=name(case))
        assert cpu.get('a') == expected, (case, a, b, cpu.get('a'), expected)
        assert cpu.sp == 0xf002
        checks += 1
print(f'{checks} extended-byte comparisons passed')
