"""Check bank-region transfers against individually expanded instructions."""
import argparse
import copy
import importlib.util
import random
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--llc', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(exist_ok=True)
    spec = importlib.util.spec_from_file_location(
        'execution', Path(__file__).with_name('z80-execution.py'))
    execution = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(execution)
    pairs = ('bc', 'de', 'hl', 'ix', 'iy')
    cases = []
    for bank in ('bc', 'de', 'hl'):
        for source in pairs:
            for destination in pairs:
                cases.append((f'${bank}_alt = ALT_SAVE16 ${source}\n'
                              f'    ${bank}_alt = BANK_INC16 ${bank}_alt\n'
                              f'    ${destination} = ALT_LOAD16 ${bank}_alt', None))
            cases.append((f'${bank}_alt = ALT_SAVE16 ${source}\n'
                          f'    ${bank}_alt = BANK_INC16 ${bank}_alt\n'
                          f'    $a = BANK_LD8ap ${bank}_alt', None))
        for destination in pairs:
            cases.append((f'${bank}_alt = BANK_DEC16 ${bank}_alt\n'
                          f'    ${destination} = ALT_LOAD16 ${bank}_alt', None))
            # The dead temporary should still become a resident memory access,
            # and the preceding resident decrement can share its activation.
            cases.append((f'${bank}_alt = BANK_DEC16 ${bank}_alt\n'
                          f'    ${destination} = ALT_LOAD16 ${bank}_alt\n'
                          f'    $a = LD8ap ${destination}', destination))
    for bank in 'bcdehl':
        cases.append((f'$a = ALT_LOAD8 ${bank}_alt\n'
                      '    ADD8ai 1, implicit-def $a, implicit-def $f, implicit $a\n'
                      f'    ${bank}_alt = ALT_SAVE8 $a', None))
        for destination in 'bcdehl':
            cases.append((f'$a = ALT_LOAD8 ${bank}_alt\n'
                          f'    ${destination}_alt = ALT_SAVE8 $a', None))
    mir = '--- |\n  target triple = "z80"\n'
    for index in range(len(cases)):
        mir += f'  define void @case{index}() nounwind {{ ret void }}\n'
    mir += '...\n'
    for index, (body, dead) in enumerate(cases):
        live = ['af', *pairs, 'bc_alt', 'de_alt', 'hl_alt']
        uses = ', '.join('implicit $' + reg for reg in live if reg != dead)
        mir += f'''---
name: case{index}
tracksRegLiveness: true
body: |
  bb.0:
    liveins: {', '.join('$' + reg for reg in live)}
    {body}
    RET16 implicit-def $sps, implicit $sps, {uses}
...
'''
    source = args.work / 'transfers.mir'
    source.write_text(mir)
    programs = []
    for mode in ('after', 'before'):
        assembly = subprocess.check_output([
            args.llc, '-mattr=+alternate-regs', '-verify-machineinstrs',
            '-start-' + mode + '=z80-bank-regions', str(source), '-o', '-'], text=True)
        (args.work / (mode + '.s')).write_text(assembly)
        programs.append(execution.Program(assembly))
    rng = random.Random(7301)
    count = 0
    for index, (_, dead) in enumerate(cases):
        for sample in range(32):
            baseline = execution.CPU(rng)
            edges = (0, 1, 255, 256, 65534, 65535)
            if sample < len(edges):
                value = edges[sample]
                for pair in pairs:
                    baseline.put(pair, value)
                for pair in ('bc', 'de', 'hl'):
                    baseline.alt[pair[0]] = value >> 8
                    baseline.alt[pair[1]] = value & 255
            candidate = copy.deepcopy(baseline)
            baseline.run(programs[0], entry='case' + str(index))
            candidate.run(programs[1], entry='case' + str(index))
            for reg in ('af', *pairs):
                if reg != dead:
                    assert candidate.get(reg) == baseline.get(reg), (index, sample, reg)
            assert candidate.alt == baseline.alt, (index, sample, 'alternate bank')
            assert candidate.sp == baseline.sp == 0xf002
            assert candidate.mem[:0xefe0] == baseline.mem[:0xefe0]
            assert candidate.mem[0xf000:] == baseline.mem[0xf000:]
            assert candidate.cycles <= baseline.cycles - 8, (index, candidate.cycles, baseline.cycles)
            count += 1
    print(f'{count} bank-transfer executions passed')


if __name__ == '__main__':
    main()
