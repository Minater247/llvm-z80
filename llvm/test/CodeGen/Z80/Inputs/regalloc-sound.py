"""Check ring-buffer and table-lookup allocation results and cycle limits."""
import argparse
import importlib.util
import json
import random
import re
from pathlib import Path

spec = importlib.util.spec_from_file_location('execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
CPU = execution.CPU


def check(assembly):
    rng = random.Random(247)
    results = {}
    for kernel in ('mix_ring', 'mix_four'):
        match = re.search(rf'^{kernel}(?:_rep0)?:.*?^\s*\.size', assembly, re.M | re.S)
        assert match, kernel
        body = match[0].rsplit('.size', 1)[0]
        cases = 0
        cycles = {}
        for n in (0, 1, 2, 7, 32, 255):
            for position in (0, 127, 250, 255):
                cpu = CPU(rng)
                initial_ix, initial_alt = cpu.get('ix'), dict(cpu.alt)
                expected = bytearray(cpu.mem)
                out, a, b, c, d, volume = 0x2113, 0x3271, 0x4307, 0x5401, 0x6581, 0x7683
                cpu.put('de', out)
                cpu.put('bc', a)
                if kernel == 'mix_ring':
                    cpu.put('l', position)
                    cpu.put('h', n)
                    for i in range(n):
                        at = out + ((position + i) & 255)
                        expected[at] = (expected[at] + expected[a + i]) & 255
                    allowed = set(range(out, out + 256))
                else:
                    cpu.put('hl', b)
                    for off, val, width in [(2, c, 2), (4, d, 2), (6, volume, 2), (8, n, 1)]:
                        cpu.write(f'({cpu.sp + off})', val, width)
                    for i in range(n):
                        expected[out + i] = sum(expected[volume + expected[p + i]] for p in (a, b, c, d)) & 255
                    allowed = set(range(out, out + n))
                cpu.writes.clear()
                cpu.run(body)
                assert cpu.mem[out:out + 256] == expected[out:out + 256], (kernel, n, position)
                assert cpu.sp == 0xf002 and cpu.get('ix') == initial_ix, (kernel, 'ABI')
                assert cpu.writes <= allowed | set(range(0xef00, 0xf000)), (kernel, 'stray write')
                if 'exx' not in body and "af'" not in body:
                    assert cpu.alt == initial_alt
                cycles[str(n)] = cpu.cycles
                cases += 1
        results[kernel] = {'executions_checked': cases, 't_states_by_sample_count': cycles}
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path)
    parser.add_argument('--max-ring-cycles', type=int)
    parser.add_argument('--max-mix-cycles', type=int)
    args = parser.parse_args()
    results = check(args.assembly.read_text())
    for name, limit in [('mix_ring', args.max_ring_cycles), ('mix_four', args.max_mix_cycles)]:
        cycles = results[name]['t_states_by_sample_count']
        if limit is not None:
            assert cycles['7'] - cycles['2'] <= 5 * limit, (name, cycles, limit)
    print(json.dumps(results, indent=2))
