"""Differentially execute physical copy cleanup with arbitrary register banks.

The reference is the same MIR emitted without the cleanup pass. Compare every
register, flags, both banks, SP and memory, including values not returned by C.
"""
import argparse
import copy
import importlib.util
import random
import subprocess
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--llc', required=True)
    ap.add_argument('--work', type=Path, required=True)
    args = ap.parse_args()
    args.work.mkdir(exist_ok=True)
    spec = importlib.util.spec_from_file_location(
        'execution', Path(__file__).with_name('z80-execution.py'))
    execution = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(execution)
    rng = random.Random(6119)
    classes = [('LD8gg', list('abcdehl')),
               ('LD8xx', list('abcde') + ['ixh', 'ixl']),
               ('LD8yy', list('abcde') + ['iyh', 'iyl'])]
    cases = []
    for _ in range(64):
        body = []
        for _ in range(12):
            opcode, regs = rng.choice(classes)
            dst, src = rng.sample(regs, 2)
            move = f'${dst} = {opcode} ${src}'
            body.append(move)
            # Keep repeated copies sometimes valid and sometimes invalid;
            # mix byte and pair writes, flags, exchanges and memory accesses.
            body += rng.choice([
                [], ['EXX16', '$e = LD8ri 91', 'EXX16'],
                ['EXX16'], ['EXAF', '$a = LD8ri 29', 'EXAF'],
                ['EXAF'], ['EX16DE'], ['EX16DE', 'EX16DE'],
                ['$de = LD16ri 1234'], ['$iy = INC16r $iy'],
                [f'${dst} = LD8ri 43'], [f'${src} = LD8ri 71'],
                ['$a = LD8ap $hl', 'LD8pa $de, $a'],
                ['$iyl = LD8yy $e', '$iyh = LD8yy $d, implicit-def $iy'],
                ['$l = LD8gg $e', '$h = LD8gg $d, implicit-def $hl'],
                ['$iyl = LD8yy $c', '$iyh = LD8yy $b, implicit-def $iy',
                 'LD8og $iy, 0, $a', '$iy = LD16ri 17325'],
                ['$ixl = LD8xx $e', '$ixh = LD8xx $d, implicit-def $ix',
                 '$a = LD8go $ix, 0', '$ix = LD16ri 11984'],
                ['$iyl = LD8yy $e', '$iyh = LD8yy $d, implicit-def $iy',
                 '$a = LD8go $iy, 1', 'LD8pg $iy, $b'],
                ['$iyl = LD8yy $e', '$iyh = LD8yy $d, implicit-def $iy',
                 '$e = INC8r $e, implicit-def $f', '$a = LD8ap $iy'],
                ['$iyl = LD8yy $e', '$iyh = LD8yy $d, implicit-def $iy',
                 '$l = LD8gg $e', '$h = LD8gg $d, implicit-def $hl',
                 '$l = LD8gp $iy', 'LD8pg $iy, $h'],
                ['ADD8ai 3, implicit-def $a, implicit-def $f, implicit $a'],
            ])
            body.append(move)
        cases.append(body)
    exchanges = {
        'EXX16': ['hl', 'de', 'bc', 'hl_alt', 'de_alt', 'bc_alt'],
        'EXAF': ['af', 'af_alt'], 'EX16DE': ['de', 'hl']}
    for body in cases:
        for i, line in enumerate(body):
            if line in exchanges:
                regs = exchanges[line]
                body[i] += ' ' + ', '.join(
                    ['implicit-def $' + r for r in regs] +
                    ['implicit $' + r for r in regs])
    ir = '\n'.join(f'define void @f{i}() {{ ret void }}' for i in range(len(cases)))
    mir = '--- |\n  target triple = "z80"\n  ' + ir.replace('\n', '\n  ') + '\n...\n'
    for i, body in enumerate(cases):
        mir += f'''---
name: f{i}
tracksRegLiveness: true
body: |
  bb.0:
    liveins: $af, $bc, $de, $hl, $ix, $iy, $af_alt, $bc_alt, $de_alt, $hl_alt
    ''' + '\n    '.join(body) + '''
    RET16 implicit-def $sps, implicit $sps, implicit $af, implicit $bc, implicit $de, implicit $hl, implicit $ix, implicit $iy
...
'''
    source = args.work / 'copies.mir'
    source.write_text(mir)
    programs = []
    for boundary in ('after', 'before'):
        assembly = subprocess.check_output([
            args.llc, '-mattr=+alternate-regs,+undoc', '-verify-machineinstrs',
            '-start-' + boundary + '=z80-late-copy', str(source), '-o', '-'], text=True)
        (args.work / (boundary + '.s')).write_text(assembly)
        programs.append(execution.Program(assembly))
    assert len(programs[1].code) < len(programs[0].code)
    for i in range(len(cases)):
        for _ in range(32):
            reference = execution.CPU(rng)
            optimized = copy.deepcopy(reference)
            reference.run(programs[0], entry=f'f{i}')
            optimized.run(programs[1], entry=f'f{i}')
            assert reference.r == optimized.r, (i, reference.r, optimized.r)
            assert reference.alt == optimized.alt, (i, reference.alt, optimized.alt)
            assert reference.sp == optimized.sp
            assert reference.mem == optimized.mem
            assert reference.reads == optimized.reads
            assert reference.writes == optimized.writes
            assert optimized.cycles <= reference.cycles
    print(f'{len(cases) * 32} differential physical-copy executions passed')


if __name__ == '__main__':
    main()
