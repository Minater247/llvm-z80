"""Check delayed extension and disconnected lane values with both allocators."""
import argparse
import importlib.util
import random
import subprocess
from pathlib import Path


def main():
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
    checks = 0
    for allocator in ('greedy', 'z80'):
        assembly = subprocess.check_output([
            args.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator, '-verify-machineinstrs',
            str(args.mir), '-o', '-'], text=True)
        (args.work / (allocator + '.s')).write_text(assembly)
        program = execution.Program(assembly)
        rng = random.Random(674)
        for value in range(256):
            data = [value] + [rng.randrange(256) for _ in range(3)]
            for name, expected in (
                    ('delay_extension', 4096 + value),
                    ('independent_values', 0x9c00 + sum(data)),
                    ('changing_high_byte', 4352 + 2 * value)):
                cpu = execution.CPU(rng)
                cpu.put('hl', 0x6000)
                cpu.mem[0x6000:0x6004] = bytes(data)
                before = bytes(cpu.mem)
                sp, ix = cpu.sp, cpu.get('ix')
                cpu.run(program, entry=name)
                assert cpu.get('hl') == expected, (allocator, name, value)
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.mem[:sp - 256] == before[:sp - 256]
                assert cpu.mem[sp:] == before[sp:]
                checks += 1
    print(f'{checks} narrow-lane executions passed')


if __name__ == '__main__':
    main()
