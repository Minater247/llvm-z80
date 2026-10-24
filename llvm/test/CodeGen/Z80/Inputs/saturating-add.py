"""Exhaustively check emitted signed-byte saturation against integer clamping."""
import argparse
import importlib.util
import random
import re
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('assembly', type=Path)
    ap.add_argument('--function', default='sat')
    args = ap.parse_args()
    spec = importlib.util.spec_from_file_location(
        'execute', Path(__file__).with_name('z80-execution.py'))
    execution = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(execution)
    assembly = args.assembly.read_text()
    rng = random.Random(0x5add)
    checked = 0
    names = [args.function]
    if args.function == 'sat':
        names += ['sat_positive', 'sat_negative', 'sat_memory']
    for name in names:
        match = re.search(r'^' + re.escape(name) + r':.*?^\s*\.size',
                          assembly, re.M | re.S)
        assert match, name
        body = match[0]
        cpu = execution.CPU(rng)
        initial, alternate = dict(cpu.r), dict(cpu.alt)
        for x in range(-128, 128):
            ys = range(-128, 128) if name in (args.function, 'sat_memory') else (
                [127] if name == 'sat_positive' else [-128])
            for y in ys:
                cpu.r = dict(initial)
                cpu.alt = dict(alternate)
                cpu.put('e', x)
                cpu.put('d', y)
                if name == 'sat_memory':
                    cpu.put('bc', 0x2000)
                    cpu.mem[0x2000] = y & 255
                # Entry flags must not influence the sum or saturation sign.
                cpu.put('f', rng.randrange(256))
                cpu.sp = 0xf000
                cpu.cycles = 0
                cpu.reads.clear()
                cpu.writes.clear()
                cpu.run(body)
                assert cpu.get('a') == (max(-128, min(127, x + y)) & 255), (name, x, y)
                if name == 'sat_memory':
                    assert cpu.reads.count(0x2000) == 1
                assert cpu.sp == 0xf002 and cpu.alt == alternate, (name, 'ABI')
                assert (cpu.r['ixh'], cpu.r['ixl']) == (initial['ixh'], initial['ixl'])
                assert cpu.writes <= set(range(0xef80, 0xf000)), (name, 'stray writes')
                checked += 1
    print(f'Checked {checked} signed saturation cases')


if __name__ == '__main__':
    main()
