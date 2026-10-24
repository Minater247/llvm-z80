"""Compare allocation costs with independently enumerated legal assignments.

All values interfere and A is fixed. Byte copies to IYL cost 8 T-states
from B/C/D/E or 16 from H/L. Pair copies to IY cost 16 or 32 respectively;
for size, the backend instead uses a three-byte PUSH/POP pair transfer.
Mixed byte/pair cases also test the opportunity cost of occupying aliases.
"""
import itertools
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

weights = [(1, 1), (1, 4, 2), (7, 1, 4, 2), (1, 7, 2, 5, 3),
           (7, 1, 5, 2, 6, 3), (1, 2, 3, 4, 5, 6), (6, 5, 4, 3, 2, 1)]
rng = random.Random(0)
weights += [tuple(rng.randint(1, 8) for _ in range(rng.randint(2, 6)))
            for _ in range(32)]
cases = [list(zip([1] * len(w), w)) for w in weights]
for _ in range(24):
    widths = rng.choice([[2, 1, 1, 1, 1], [2, 2, 1, 1], [2, 2, 2]])[:]
    rng.shuffle(widths)
    cases.append([(width, rng.randint(1, 8)) for width in widths])
text = ['--- |', '  target triple = "z80"']
for i in range(len(cases)):
    size = 'optsize ' if i % 2 else ''
    text.append(f'  define void @case{i}() {size}{{ ret void }}')
text.append('...')
for i, values in enumerate(cases):
    text += ['---', f'name: case{i}', 'tracksRegLiveness: true', 'body: |',
             '  bb.0:', '    liveins: $a']
    for r, (width, _) in enumerate(values):
        bits = width * 8
        text.append(f'    %{r}:g{bits} = LD{bits}ri {r + 1}')
    for r, (width, count) in enumerate(values):
        dest = '$iyl' if width == 1 else '$iy'
        for _ in range(count):
            text += [f'    {dest} = COPY %{r}', f'    KILL {dest}']
    text += ['    KILL ' + ', '.join(f'%{r}' for r in range(len(values))) + ', $a',
             '    RET16 implicit-def $sps, implicit $sps', '...']
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'oracle.mir'
    path.write_text('\n'.join(text) + '\n')
    result = subprocess.run([sys.argv[1], '-mattr=+undoc',
                             '-run-pass=z80-regalloc,virtregrewriter',
                             '-verify-machineinstrs', str(path), '-o', '-'],
                            check=True, capture_output=True, text=True).stdout


def cost(reg, size):
    if len(reg) == 2:
        return 3 if size else (32 if reg == 'hl' else 16)
    return (4 if reg in 'hl' else 2) if size else (16 if reg in 'hl' else 8)


for i, values in enumerate(cases):
    body = re.search(r'name: *case' + str(i) + r'\n.*?body: *\|\n(.*?)\n\.\.\.',
                     result, re.S).group(1)
    assigned = dict((int(value) - 1, reg) for reg, value in
                    re.findall(r'\$(\w+) = LD(?:8|16)ri (\d+)', body))
    assert len(assigned) == len(values), (i, 'unexpected spill', body)
    actual = sum(count * cost(assigned[r], i % 2)
                 for r, (_, count) in enumerate(values))
    domains = ['bcdehl' if width == 1 else ['bc', 'de', 'hl']
               for width, _ in values]
    optimum = float('inf')
    for assignment in itertools.product(*domains):
        units = ''.join(assignment)
        if len(units) != len(set(units)):
            continue
        candidate = sum(count * cost(reg, i % 2)
                        for reg, (_, count) in zip(assignment, values))
        optimum = min(optimum, candidate)
    assert actual == optimum, (i, values, assigned, actual, optimum)
print(f'{len(cases)} allocations match the exhaustive optimum')
