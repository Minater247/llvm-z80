"""Check saturating byte lookups, page wrap and memory access ordering."""
import argparse
import copy
import importlib.util
import json
import random
import re
import statistics
from pathlib import Path


def kernel_body(body):
    """Remove only the outer frame/return shell of this single-exit kernel.

    Fail if the saved/frame register is used by the body: silently discarding
    a real frame would change execution. Inner work, transfers and calls are
    never stripped. This helper only handles single-exit leaf shapes; the
    persistent-state checker executes complete programs with real calls.
    """
    lines = body.splitlines()
    ops = [(i, line.split(';', 1)[0].strip()) for i, line in enumerate(lines)]
    ops = [(i, re.sub(r'\s+', ' ', line)) for i, line in ops
           if line and not line.startswith('.') and not line.endswith(':')]
    assert ops[-1][1] == 'ret' and sum(op == 'ret' for _, op in ops) == 1
    removed = [ops[-1]]
    stream = False
    prefix = ['ld iy, 0', 'add iy, sp', 'ld a, h', 'ld h, b',
              'ld l, c', 'ld sp, hl', 'ld h, a']
    if [op for _, op in ops[:7]] == prefix:
        assert ops[7][1] == 'pop bc' and ops[-2][1] == 'ld sp, iy'
        assert not any(re.search(r'\biy[hl]?\b', op) for _, op in ops[7:-2])
        removed += ops[:7] + [ops[-2]]
        stream = True
    elif ops[0][1] == 'push ix' and ops[-2][1] == 'pop ix':
        assert not any(re.search(r'\bix[hl]?\b', op) for _, op in ops[1:-2])
        removed += [ops[0], ops[-2]]
    elif [op for _, op in ops[:2]] == ['ld iy, 0', 'add iy, sp']:
        assert not any(re.search(r'\biy[hl]?\b', op) for _, op in ops[2:-1])
        removed += ops[:2]
    else:
        raise AssertionError('Unrecognized outer setup: review measurement boundary')
    assert not any(re.search(r'\bsp\b', op) for item, op in ops
                   if (item, op) not in removed), 'Body depends on the outer frame'
    removed_lines = {i for i, _ in removed}
    core = '\n'.join(line for i, line in enumerate(lines) if i not in removed_lines)
    # RET is an execution sentinel, subtracted from the measured work.
    return core + '\nret\n', [op for _, op in sorted(removed)], stream


def check(bodies, random_cases=512):
    spec = importlib.util.spec_from_file_location(
        'execute', Path(__file__).with_name('z80-execution.py'))
    execution = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(execution)
    rng = random.Random(0x24780)
    edges = (-128, -127, -64, -1, 0, 1, 63, 126, 127)
    cases = [(position, x, y, -1 - x, -1 - y)
             for position in (0, 127, 254, 255) for x in edges for y in edges]
    cases += [(rng.randrange(256), *(rng.randrange(-128, 128) for _ in range(4)))
              for _ in range(random_cases)]
    times = {name: [] for name in bodies}
    core_times_by_name = {name: [] for name in bodies}
    cores, excluded, streams = {}, {}, {}
    for name, body in bodies.items():
        cores[name], excluded[name], streams[name] = kernel_body(body)
    rom_reads = {name: set() for name in bodies}
    for position, x, y, x2, y2 in cases:
        initial = execution.CPU(rng)
        initial.sp = 0x1fe0
        out, table, src = 0x0800, 0x1000, 0x8000 + rng.randrange(0x1000)
        indices = rng.sample(range(256), 2)
        initial.mem[src:src + 2] = bytes(indices)
        initial.mem[table + indices[0]] = y & 255
        initial.mem[table + indices[1]] = y2 & 255
        destinations = [out + position, out + ((position + 1) & 255)]
        initial.mem[destinations[0]] = x & 255
        initial.mem[destinations[1]] = x2 & 255
        initial.put('de', out + position)
        initial.put('bc', src)
        initial.put('hl', table + rng.randrange(256))
        expected = [max(-128, min(127, x + y)) & 255,
                    max(-128, min(127, x2 + y2)) & 255]
        for name, body in bodies.items():
            cpu = copy.deepcopy(initial)
            cpu.run(body)
            assert [cpu.mem[a] for a in destinations] == expected, (name, position, x, y, x2, y2)
            assert cpu.sp == initial.sp + 2 and cpu.get('ix') == initial.get('ix'), (name, 'ABI')
            assert cpu.writes <= set(destinations) | set(range(initial.sp - 128, initial.sp)), (name, 'stray writes')
            if name.endswith('_default'):
                assert cpu.alt == initial.alt, (name, 'reserved bank')
            reads = sum(a >= 0x8000 for a in cpu.reads)
            assert reads == 2, (name, 'ROM reads', reads)
            rom_reads[name].add(reads)
            times[name].append(cpu.cycles)
            core = copy.deepcopy(initial)
            if streams[name]:
                core.sp = src
            core.run(cores[name])
            assert [core.mem[a] for a in destinations] == expected, (name, 'core result')
            assert sum(a >= 0x8000 for a in core.reads) == 2, (name, 'core ROM reads')
            if name.endswith('_default'):
                assert core.alt == initial.alt, (name, 'core reserved bank')
            core_times_by_name[name].append(core.cycles - 10)
    # Also check overlapping RAM pointers against each source's own read order.
    # Sequential C must observe a first store that changes its second input;
    # explicitly prefetched loads must retain the earlier byte.
    alias_cases = 0
    for displacement in (-1, 0, 1):
        for _ in range(32):
            initial = execution.CPU(rng)
            initial.sp = 0x1fe0
            src, table = 0x2040, 0x1000
            out = src + displacement
            initial.put('bc', src)
            initial.put('de', out)
            initial.put('hl', table + rng.randrange(256))
            for name, body in bodies.items():
                expected = bytearray(initial.mem)
                prefetched = bytes(expected[src:src+2])
                for j in range(2):
                    index = prefetched[j] if streams[name] else expected[src+j]
                    lhs, rhs = expected[out+j], expected[table+index]
                    lhs = lhs-256 if lhs >= 128 else lhs
                    rhs = rhs-256 if rhs >= 128 else rhs
                    expected[out+j] = max(-128, min(127, lhs+rhs)) & 255
                cpu = copy.deepcopy(initial)
                cpu.run(body)
                assert cpu.mem[out:out+2] == expected[out:out+2], (name, 'alias read order')
                assert cpu.sp == initial.sp+2 and cpu.get('ix') == initial.get('ix'), (name, 'alias ABI')
                assert cpu.writes <= set(range(out, out+2)) | set(range(initial.sp-128, initial.sp))
            alias_cases += 1
    def metrics(values):
        return {'min_t_states': min(values), 'max_t_states': max(values),
                'mean_t_states': statistics.mean(values)}
    return {
        'cases_per_implementation': len(cases),
        'alias_legality_cases_per_implementation': alias_cases,
        'comparison': {name:
                       dict(metrics(values), rom_bytes_read=sorted(rom_reads[name]))
                       for name, values in core_times_by_name.items()},
        'excluded_outer_instructions': excluded,
        'entry_exit_inclusive': {name: metrics(values)
                       for name, values in times.items()},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('assembly', type=Path)
    ap.add_argument('--max-cycles', type=int, required=True)
    ap.add_argument('--alternate-registers', action='store_true')
    args = ap.parse_args()
    match = re.search(r'^pcm_mix2:.*?^\s*\.size', args.assembly.read_text(), re.M | re.S)
    assert match, 'Missing pcm_mix2 function'
    name = 'candidate_bank' if args.alternate_registers else 'candidate_default'
    report = check({name: match[0]})
    assert report['comparison'][name]['max_t_states'] <= args.max_cycles, report
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
