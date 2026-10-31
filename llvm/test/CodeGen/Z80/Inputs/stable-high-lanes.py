"""Execute separated variable-high lanes and local address constraints."""
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
    count = 0
    for allocator in ('greedy', 'z80'):
        for bank in (False, True):
            assembly = subprocess.check_output([
                args.llc, '-start-before=z80-machine-prera-opt',
                '-regalloc=' + allocator, '-verify-machineinstrs',
                '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
                str(args.mir), '-o', '-'], text=True)
            (args.work / (allocator + str(bank) + '.s')).write_text(assembly)
            program = execution.Program(assembly)
            rng = random.Random(924)
            for high in range(256):
                for low in (0, 127, 254, 255):
                    cpu = execution.CPU(rng)
                    pointer = high * 256 + low
                    # Keep the lookup away from the return address and saves,
                    # including wrap from the last address to the first page.
                    cpu.sp = ((high + 128) & 255) * 256 + 128
                    cpu.put('d', high)
                    cpu.put('c', low)
                    expected = cpu.mem[(pointer + 2) & 65535]
                    sp, ix, alt = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                    cpu.run(program, entry='lookup')
                    assert cpu.get('hl') == pointer
                    assert cpu.get('a') == expected
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    if not bank:
                        assert cpu.alt == alt
                    count += 1
    print(f'{count} variable-high lane executions passed')


if __name__ == '__main__':
    main()
