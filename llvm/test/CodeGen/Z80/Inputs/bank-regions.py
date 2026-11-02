"""Differentially execute bank arithmetic regions against unfused transfers.

Randomize all registers, both banks, flags, and memory. Compare live results,
non-stack memory and unrelated bank contents. The compiler's machine verifier
checks every input and transformed function as well.
"""
import argparse
import copy
import importlib.util
import random
import re
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
    cases = []
    for bank in ('bc', 'de', 'hl'):
        for main in ('bc', 'de', 'hl'):
            # Increment/decrement chains, memory accesses and unbanked
            # accumulator arithmetic. All register pairs can address A.
            for body in (
                f'${main} = INC16r ${main}\n    ${main} = INC16r ${main}',
                f'${main} = DEC16r ${main}\n    $a = LD8ap ${main}\n'
                f'    ADD8ai 13, implicit-def $a, implicit-def $f, implicit $a\n'
                f'    LD8pa ${main}, $a',
                f'LD8pa ${main}, $a\n    ${main} = INC16r ${main}',
            ):
                for export in (False, True):
                    live = 'af' + ''.join(r for r in 'bcdehl' if export or r not in main)
                    cases.append((bank, main, body, live, True))
        for byte in ('b', 'c', 'd', 'e'):
            for preserve_a in (False, True):
                # Exercise live byte results, imports, and A preservation.
                for store in (False, True):
                    body = (f'LD8pg $hl, ${byte}' if store else f'${byte} = LD8gp $hl')
                    body += '\n    $hl = INC16r $hl'
                    live = 'bcdef' + ('a' if preserve_a else '')
                    cases.append((bank, 'hl', body, live, True))
    # Legitimate flag updates, two-address byte arithmetic and aliasing halves.
    for bank, byte in [('hl', 'l'), ('de', 'e'), ('bc', 'c')]:
        cases.append((bank, bank,
                      f'${byte} = INC8r ${byte}, implicit-def $f\n'
                      f'    ${bank} = INC16r ${bank}', 'af', True))
    # Two arithmetic regions separated only by unbanked accumulator work can
    # retain the active bank, eliminating the middle pair of EXX instructions.
    fused_cases = {len(cases)}
    cases.append(('hl', 'hl',
                  '$hl = INC16r $hl\n    $hl_alt = ALT_SAVE16 $hl\n'
                  '    ADD8ai 7, implicit-def $a, implicit-def $f, implicit $a\n'
                  '    $hl = ALT_LOAD16 $hl_alt\n    LD8pa $hl, $a\n'
                  '    $hl = INC16r $hl', 'afbcde', True))
    # Atomic saturation can stay inside an active bank when its source is
    # immediate or indexed; its internal branch leaves the bank unchanged.
    for operation in ('SADDSAT8ai 13', 'SADDSAT8ap $iy'):
        fused_cases.add(len(cases))
        cases.append(('hl', 'hl',
                      '$hl = INC16r $hl\n    $hl_alt = ALT_SAVE16 $hl\n'
                      f'    {operation}, implicit-def $a, implicit-def $f, implicit $a\n'
                      '    $hl = ALT_LOAD16 $hl_alt\n    LD8pa $hl, $a\n'
                      '    $hl = INC16r $hl', 'afbcde', True))
    # Unsafe/unprofitable cases must retain transfers: a second banked input,
    # A already used by the body, preserving A would overwrite computed flags,
    # a stack-changing instruction, and an incompatible address register.
    cases += [
        ('hl', 'hl', '$hl = ADD16ao $hl, $bc, implicit-def $f', 'afbcde', False),
        ('hl', 'hl', '$c = LD8gp $hl\n    $a = LD8gg $c', 'abcde', False),
        ('hl', 'hl', '$c = LD8gp $hl\n    $c = INC8r $c, implicit-def $f', 'afbcde', False),
        ('hl', 'hl', 'PUSH16r $hl, implicit-def $sps, implicit $sps\n'
         '    $hl = POP16r implicit-def $sps, implicit $sps', 'afbcde', False),
        ('de', 'hl', '$l = LD8gp $hl', 'afbcde', False),
    ]
    # Index-register temporaries must not force transfers out of the bank.
    for bank in ('bc', 'de', 'hl'):
        for export in (False, True):
            live = list('afbcdehl') + (['iyh', 'iyl'] if export else [])
            cases.append((bank, 'iy',
                          '$iyl = INC8r $iyl, implicit-def $f\n'
                          '    $a = LD8ap $iy', live, True))
    read_only = set()
    needs_free_af = set()
    for bank in ('bc', 'de', 'hl'):
        for main in ('bc', 'de', 'hl', 'iy'):
            for export in (False, True):
                live = [r for r in 'afbcdehl' if export or r not in main]
                if main == 'iy' and export:
                    live += ['iyh', 'iyl']
                read_only.add(len(cases))
                cases.append((bank, main, f'$a = LD8ap ${main}', live, not export))
        # A byte result can replace either address lane or an independent
        # register. Preserve A and flags when the caller keeps them live.
        for byte in 'bcdehl':
            for preserve_a in (False, True):
                for keep_high in (False, True):
                    live = [r for r in 'fbcde' if r != byte] + [byte]
                    if preserve_a:
                        live += ['a']
                    if keep_high:
                        live += ['l' if byte == 'h' else 'h']
                    i = len(cases)
                    read_only.add(i)
                    if preserve_a:
                        needs_free_af.add(i)
                    cases.append((bank, 'hl', f'${byte} = LD8gp $hl',
                                  live, not keep_high))
    count = 0
    for busy_af in (False, True):
        work = args.work / ('occupied-af' if busy_af else 'free-af')
        work.mkdir(exist_ok=True)
        # Exercise the existing SP-based static-stack ABI's unwind path too.
        attr = ' "static_stack_needs_ix"' if busy_af else ''
        ir = '\n'.join(f'define void @f{i}() uwtable{attr} {{ ret void }}'
                       for i in range(len(cases)))
        mir = '--- |\n  target triple = "z80"\n  ' + ir.replace('\n', '\n  ') + '\n...\n'
        for i, (bank, main, body, live, _) in enumerate(cases):
            operands = ', '.join(f'implicit ${r}' for r in live)
            if busy_af:
                operands += ', implicit $af_alt'
            save = '' if i in read_only else f'${bank}_alt = ALT_SAVE16 ${main}'
            mir += f'''---
name: f{i}
tracksRegLiveness: true
body: |
  bb.0:
    liveins: $bc, $de, $hl, $af, $iy, $bc_alt, $de_alt, $hl_alt
    ${main} = ALT_LOAD16 ${bank}_alt
    {body}
    {save}
    RET16 implicit-def $sps, implicit $sps, {operands}
...
'''
        source = work / 'input.mir'
        source.write_text(mir)
        transformed = work / 'regions.mir'
        subprocess.run([args.llc, '-mattr=+alternate-regs', '-verify-machineinstrs',
                        '-run-pass=z80-bank-regions', str(source), '-o', str(transformed)], check=True)
        text = transformed.read_text()
        if busy_af:
            assert 'CFI_INSTRUCTION adjust_cfa_offset 2' in text
            assert 'CFI_INSTRUCTION adjust_cfa_offset -2' in text
        disabled = subprocess.check_output(
            [args.llc, '-mattr=-alternate-regs', '-verify-machineinstrs',
             '-run-pass=z80-bank-regions', str(source), '-o', '-'], text=True)
        assert 'EXX16' not in disabled
        for i, (*_, expected) in enumerate(cases):
            expected &= not (busy_af and i in needs_free_af)
            body = re.search(rf'^name: +f{i}\n.*?^\.\.\.', text, re.M | re.S)[0]
            assert ('EXX16' in body) == expected, (i, cases[i], body)
            if i in fused_cases:
                assert body.count('EXX16') == 2, body
        outputs = []
        for name, path in [('reference', source), ('regions', transformed)]:
            output = work / (name + '.s')
            # Start at the assembly printer, bypassing all other late transforms.
            subprocess.run([args.llc, '-mattr=+alternate-regs', '-start-after=z80-branch-select',
                            str(path), '-o', str(output)], check=True)
            outputs.append(output.read_text())
        rng = random.Random(24703)
        for i, (_, main, _, live, optimized) in enumerate(cases):
            optimized &= not (busy_af and i in needs_free_af)
            bodies = [re.search(rf'^f{i}:.*?^\s*\.size', s, re.M | re.S)[0]
                      for s in outputs]
            for _ in range(32):
                before = execution.CPU(rng)
                # Keep pointers away from transient stack storage.
                before.put('iy', rng.randrange(0x2000, 0x6000))
                for r in ('bc', 'de', 'hl'):
                    before.put(r, rng.randrange(0x2000, 0x6000))
                    before.alt[r[0]] = rng.randrange(0x20, 0x60)
                reference, actual = copy.deepcopy(before), copy.deepcopy(before)
                reference.run(bodies[0])
                actual.run(bodies[1])
                assert actual.sp == reference.sp, i
                registers = list(live) + ['ixh', 'ixl']
                if main != 'iy':
                    registers += ['iyh', 'iyl']
                for r in registers:
                    assert actual.r[r] == reference.r[r], (i, r, cases[i])
                for r in 'afbcdehl':
                    if r in 'af' and "af'" in bodies[1]:
                        continue  # Unallocated AF' is available as region scratch.
                    assert actual.alt[r] == reference.alt[r], (i, 'alternate ' + r, cases[i])
                assert actual.mem[:0xeff0] == reference.mem[:0xeff0], (i, 'memory')
                assert actual.mem[0xf000:] == reference.mem[0xf000:], (i, 'memory')
                assert actual.cycles <= reference.cycles, (i, actual.cycles, reference.cycles)
                if optimized:
                    assert actual.cycles < reference.cycles, i
                count += 1
    print(f'{count} randomized bank-region executions checked')


if __name__ == '__main__':
    main()
