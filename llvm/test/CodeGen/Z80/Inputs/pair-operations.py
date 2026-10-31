"""Execute pair alternatives and banked pointer operations with arbitrary state."""
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
    cases = []
    for dst in ('hl', 'de', 'bc', 'ix', 'iy'):
        for src in ('bc', 'de'):
            cases.append((f'${dst} = ADD16rr ${dst}, ${src}, implicit-def $f',
                          dst, src))
    for dst in ('hl', 'de', 'bc', 'ix', 'iy', 'hl_alt', 'de_alt', 'bc_alt'):
        for src in ('bc', 'de', 'bc_alt', 'de_alt'):
            cases.append((f'${dst} = BANK_ADD16 ${dst}, ${src}, implicit-def $f',
                          dst, 'add:' + src))
    for pair in ('hl', 'de', 'bc'):
        cases.append((f'''${pair}_alt = BANK_LD16ri 24576
    ${pair}_alt = BANK_INC16 ${pair}_alt
    BANK_LD8pa ${pair}_alt, $a :: (volatile store (s8))
    ${pair}_alt = BANK_DEC16 ${pair}_alt
    $a = BANK_LD8ap ${pair}_alt :: (volatile load (s8))''', pair, None))
    for dst in ('hl_alt', 'de_alt', 'bc_alt'):
        for src in ('hl_alt', 'de_alt', 'bc_alt', 'hl', 'bc', 'de', 'ix', 'iy'):
            cases.append((f'${dst} = COPY ${src}', dst, 'copy:' + src))
    for dst in ('hl', 'bc', 'de', 'ix', 'iy'):
        for src in ('hl_alt', 'de_alt', 'bc_alt'):
            cases.append((f'${dst} = COPY ${src}', dst, 'copy:' + src))
    mir = '--- |\n  target triple = "z80"\n'
    for i in range(len(cases)):
        mir += f'  define void @f{i}() {{ ret void }}\n'
    mir += '...\n'
    for i, (body, _, _) in enumerate(cases):
        mir += f'''---
name: f{i}
tracksRegLiveness: true
body: |
  bb.0:
    liveins: $af, $bc, $de, $hl, $ix, $iy, $bc_alt, $de_alt, $hl_alt
    {body}
    RET16 implicit-def $sps, implicit $sps, implicit $af, implicit $bc, implicit $de, implicit $hl, implicit $ix, implicit $iy, implicit $bc_alt, implicit $de_alt, implicit $hl_alt
...
'''
    source = args.work / 'pairs.mir'
    source.write_text(mir)
    assembly = subprocess.check_output([
        args.llc, '-mattr=+alternate-regs,+undoc', '-verify-machineinstrs',
        '-start-before=postrapseudos', str(source), '-o', '-'], text=True)
    (args.work / 'pairs.s').write_text(assembly)
    program = execution.Program(assembly)
    rng = random.Random(8291)
    for i, (_, dst, src) in enumerate(cases):
        for _ in range(64):
            cpu = execution.CPU(rng)
            expected = copy.deepcopy(cpu)
            if src is None:
                expected.alt[dst[0]], expected.alt[dst[1]] = 0x60, 0
                expected.mem[0x6001] = expected.r['a']
                expected.r['a'] = expected.mem[0x6000]
            elif src.startswith('copy:'):
                src = src[5:]
                value = ((expected.alt[src[0]] << 8) | expected.alt[src[1]]
                         if src.endswith('_alt') else expected.get(src))
                if dst.endswith('_alt'):
                    expected.alt[dst[0]], expected.alt[dst[1]] = value >> 8, value & 255
                else:
                    expected.put(dst, value)
                src = 'copy:' + src
            else:
                add_src = src[4:] if src.startswith('add:') else src
                def value(reg):
                    return ((expected.alt[reg[0]] << 8) | expected.alt[reg[1]]
                            if reg.endswith('_alt') else expected.get(reg))
                a, b = value(dst), value(add_src)
                result = a + b
                if dst.endswith('_alt'):
                    expected.alt[dst[0]] = (result >> 8) & 255
                    expected.alt[dst[1]] = result & 255
                else:
                    expected.put(dst, result & 65535)
                expected.r['f'] = ((expected.r['f'] & 0xc4) |
                                   ((result >> 8) & 0x28) |
                                   (0x10 if (a ^ b ^ result) & 0x1000 else 0) |
                                   (result > 65535))
            cpu.run(program, entry=f'f{i}')
            assert cpu.r == expected.r, (i, dst, src, cpu.r, expected.r)
            assert cpu.alt == expected.alt, (i, dst, src, cpu.alt, expected.alt)
            assert cpu.sp == 0xf002, (i, cpu.sp)
            assert cpu.mem[:0xeff0] == expected.mem[:0xeff0], i
            assert cpu.mem[0xf000:] == expected.mem[0xf000:], i
    spill_assembly = subprocess.check_output([
        args.llc, '-mattr=+alternate-regs', '-verify-machineinstrs',
        '-start-before=z80-post-rewrite',
        str(Path(__file__).resolve().parents[1] / 'bank-spill.mir'),
        '-o', '-'], text=True)
    (args.work / 'spill.s').write_text(spill_assembly)
    spill_program = execution.Program(spill_assembly)
    for _ in range(64):
        cpu = execution.CPU(rng)
        before = copy.deepcopy(cpu)
        cpu.run(spill_program, entry='bank_spill')
        for reg in ('a', 'bc', 'de', 'hl', 'ix'):
            assert cpu.get(reg) == before.get(reg), ('spill', reg)
        assert cpu.alt == before.alt, ('spill', cpu.alt, before.alt)
        assert cpu.sp == 0xf002
        assert cpu.mem[:0xeff0] == before.mem[:0xeff0]
        assert cpu.mem[0xf000:] == before.mem[0xf000:]
    print(f'{len(cases) * 64} pair and bank executions and 64 spill executions passed')


if __name__ == '__main__':
    main()
