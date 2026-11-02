"""Check banked stores, wraparound, live A and unchanged flags in both allocators."""
import argparse
import importlib.util
from pathlib import Path
import random
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--llc', required=True)
parser.add_argument('--mir', type=Path, required=True)
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
args.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
rng = random.Random(9431)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        source = args.mir
        if not bank:
            source = args.work / 'primary.mir'
            source.write_text(args.mir.read_text().replace(', $iy', '')
                              .replace(', implicit $iy', ''))
        assembly = subprocess.check_output([
            args.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(source), '-o', '-'], text=True)
        (args.work / (allocator + str(bank) + '.s')).write_text(assembly)
        program = execution.Program(assembly)
        for name in ('free_accumulator', 'live_accumulator'):
            for pointer in (0, 255, 0x2000, 0x7fff, 0xffff):
                for value in range(256):
                    cpu = execution.CPU(rng)
                    cpu.put('hl', pointer)
                    cpu.put('c', value)
                    saved = {r: cpu.get(r) for r in ('bc', 'de', 'ix', 'f')}
                    sp, alternate = cpu.sp, dict(cpu.alt)
                    cpu.run(program, entry=name)
                    expected_a = 73 if name == 'free_accumulator' else 61
                    assert cpu.mem[pointer] == value
                    assert cpu.mem[(pointer + 1) & 65535] == expected_a
                    assert cpu.get('a') == expected_a
                    result = (value - 91) & 255
                    flags = ((result & 0xa8) | (0x40 if result == 0 else 0) |
                             (0x10 if (value ^ 91 ^ result) & 0x10 else 0) |
                             (4 if (value ^ 91) & (value ^ result) & 0x80 else 0) |
                             2 | (value < 91))
                    assert cpu.get('f') == flags
                    assert cpu.get('bc') == saved['bc']
                    assert cpu.sp == sp + 2
                    assert cpu.writes <= set(range(sp - 256, sp)) | {pointer, (pointer + 1) & 65535}
                    if name == 'free_accumulator':
                        assert cpu.get('hl') == 42
                        for r in ('de', 'ix'):
                            assert cpu.get(r) == saved[r], (allocator, bank, r)
                    if not bank:
                        assert cpu.alt == alternate
                    checks += 1
print(f'{checks} accumulator-store executions passed')
