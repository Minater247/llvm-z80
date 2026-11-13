"""Execute emitted bank transfers against randomized register states.

Checks all byte/pair sources, destinations and bank slots in both native modes,
including preservation of both flag banks, unrelated registers and SP.
Explicit alternate-flag clobbers also exercise the shorter AF copy.
"""
import argparse
import random
import re
import subprocess
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--llc', required=True)
    ap.add_argument('--work', required=True)
    args = ap.parse_args()
    work = Path(args.work)
    work.mkdir(exist_ok=True)
    rng = random.Random(1907)
    checked = 0
    for bits, triple in [(16, 'z80'), (24, 'ez80')]:
        cases = []
        prefix = 'u' if bits == 24 else ''
        for save in (True, False):
            for reg in ['a', 'b', 'c', 'd', 'e', 'h', 'l', 'ixh', 'ixl', 'iyh', 'iyl']:
                for alt in ('a', 'b', 'c', 'd', 'e', 'h', 'l'):
                    cases.append((save, 8, reg, alt + '_alt', False, None))
            for reg in ['bc', 'de', 'hl', 'ix', 'iy']:
                for alt in ['bc', 'de', 'hl']:
                    cases.append((save, bits, prefix + reg, prefix + alt + '_alt', False, None))
        cases.append((True, 8, 'a', 'a_alt', True, None))
        for scratch in ('b', 'c', 'd', 'e', 'h', 'l'):
            for save in (True, False):
                cases.append((save, 8, 'a', 'a_alt', False, scratch))
        ir = '\n'.join(f'define void @f{i}() {{ ret void }}' for i in range(len(cases)))
        mir = f'--- |\n  target triple = "{triple}"\n  ' + ir.replace('\n', '\n  ') + '\n...\n'
        for i, (save, width, reg, alt, clobber_flags, scratch) in enumerate(cases):
            dst, src = (alt, reg) if save else (reg, alt)
            op = 'SAVE' if save else 'LOAD'
            flags_def = ', implicit-def dead $f_alt' if clobber_flags else ''
            if scratch:
                flags_def += f', implicit-def dead ${scratch}'
            mir += f'''---
name: f{i}
tracksRegLiveness: false
body: |
  bb.0:
    ${dst} = ALT_{op}{width} ${src}{flags_def}
    RET{bits} implicit-def $sp{'l' if bits == 24 else 's'}, implicit $sp{'l' if bits == 24 else 's'}
...
'''
        path = work / (triple + '.mir')
        path.write_text(mir)
        asm = subprocess.check_output([args.llc, '-mattr=+alternate-regs,+undoc',
                                       '-start-after=prologepilog', str(path), '-o', '-'], text=True)
        bodies = re.findall(r'^f\d+:.*?^\s*\.size', asm, re.M | re.S)
        assert len(bodies) == len(cases)
        for (save, width, reg, alt, clobber_flags, scratch), body in zip(cases, bodies):
            code = [line.strip() for line in body.splitlines() if re.match(r'\t[a-z]', line)]
            for _ in range(32):
                regs = {r: rng.randrange(256) for r in
                        ['a', 'f', 'b', 'c', 'd', 'e', 'h', 'l', 'ixh', 'ixl', 'iyh', 'iyl',
                         'bcu', 'deu', 'hlu', 'ixu', 'iyu']}
                bank = {r: rng.randrange(256) for r in regs}
                initial, alternate = dict(regs), dict(bank)
                stack = []
                def read(r, state):
                    if r in state:
                        return state[r]
                    names = ['ixh', 'ixl'] if r == 'ix' else ['iyh', 'iyl'] if r == 'iy' else list(r)
                    value = (state[names[0]] << 8) | state[names[1]]
                    if bits == 24 and r != 'af':
                        value |= state[r + 'u'] << 16
                    return value
                def write(r, value, state):
                    if r in state:
                        state[r] = value & 255
                        return
                    names = ['ixh', 'ixl'] if r == 'ix' else ['iyh', 'iyl'] if r == 'iy' else list(r)
                    state[names[0]], state[names[1]] = (value >> 8) & 255, value & 255
                    if bits == 24 and r != 'af':
                        state[r + 'u'] = value >> 16
                for line in code:
                    op, *tail = line.split(None, 1)
                    operands = tail[0] if tail else ''
                    if op == 'push':
                        stack.append(read(operands, regs))
                    elif op == 'pop':
                        write(operands, stack.pop(), regs)
                    elif op == 'exx':
                        for r in ['b', 'c', 'd', 'e', 'h', 'l'] + (['bcu', 'deu', 'hlu'] if bits == 24 else []):
                            regs[r], bank[r] = bank[r], regs[r]
                    elif op == 'ex' and operands == "af, af'":
                        for r in ['a', 'f']:
                            regs[r], bank[r] = bank[r], regs[r]
                    elif op == 'ld':
                        dst, src = operands.split(', ')
                        write(dst, read(src, regs), regs)
                    elif op != 'ret':
                        raise AssertionError(line)
                expected, expected_alt = dict(initial), dict(alternate)
                main = reg.removeprefix('u')
                shadow = alt.removesuffix('_alt').removeprefix('u')
                if save:
                    write(shadow, read(main, initial), expected_alt)
                    if clobber_flags:
                        expected_alt['f'] = initial['f']
                else:
                    write(main, read(shadow, alternate), expected)
                if scratch:
                    expected[scratch] = initial['a'] if save else alternate['a']
                assert not stack and regs == expected and bank == expected_alt, (bits, reg, alt, save, code)
                checked += 1
    print(f'{checked} randomized bank-transfer executions passed')


if __name__ == '__main__':
    main()
