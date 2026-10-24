#!/usr/bin/env python3
"""Check persistent PCM cursors and actual calls through an ordinary-C caller."""
import copy
import importlib.util
import random
import re
import statistics
from pathlib import Path


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace('-', '_'), Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


execution = load('z80-execution')


def function(assembly, name):
    match = re.search(r'^' + name + r':.*?^\s*\.size[^\n]*', assembly, re.M | re.S)
    assert match, ('Missing function', name)
    # Independently compiled translation units reuse local label numbers.
    return re.sub(r'\.L([\w.]+)', r'.L' + name + r'_\1', match[0])


def outer_shell(program):
    """Exclude only the first function's frame and final return transfer.

    Execute every instruction, including the shell, to preserve its real stack
    layout. None of the nested functions' setup, calls or returns is excluded.
    Fail on an unfamiliar shape so a changed compiler cannot hide body work.
    """
    code = program.code
    assert code[:3] == [('push', ['ix']), ('ld', ['ix', '0']),
                       ('add', ['ix', 'sp'])], code[:3]
    end = next(i for i, (op, _) in enumerate(code) if op == 'ret')
    assert code[end-2:end+1] == [('ld', ['sp', 'ix']), ('pop', ['ix']), ('ret', [])]
    excluded = set(range(3)) | set(range(end-2, end+1))
    i = 3
    # Fixed stack allocation uses pushes/decrements or a negative IY offset.
    while code[i] in [('push', ['iy']), ('push', ['hl']), ('dec', ['sp'])]:
        excluded.add(i)
        i += 1
    if (code[i][0] == 'ld' and code[i][1][0] == 'iy' and
            re.fullmatch(r'-\d+', code[i][1][1])):
        assert code[i+1:i+3] == [('add', ['iy', 'sp']), ('ld', ['sp', 'iy'])]
        excluded.update(range(i, i+3))
    # A final reload into A is the scalar return transfer, not mixer work.
    if (code[end-3][0] == 'ld' and code[end-3][1][0] == 'a' and
            re.fullmatch(r'\(ix - \d+\)', code[end-3][1][1])):
        excluded.add(end-3)
    return excluded


def check(runner, workers, random_cases=128, calls_per_block=1):
    rng = random.Random(0x24781)
    programs = {name: execution.Program(runner + '\n' + worker)
                for name, worker in workers.items()}
    exclusions = {name: outer_shell(p) for name, p in programs.items()}
    times = {name: {n: [] for n in (0, 1, 2)} for name in workers}
    inclusive = copy.deepcopy(times)
    cases = [(pos, blocks, 0, 0) for pos in (0, 127, 252, 254, 255)
             for blocks in (0, 1, 2)]
    cases += [(pos, blocks, x, y) for pos in (0, 254, 255)
              for blocks in (1, 2) for x in (-128, -1, 0, 127)
              for y in (-128, -1, 0, 127)]
    cases += [(rng.randrange(256), rng.randrange(3),
               rng.randrange(-128, 128), rng.randrange(-128, 128))
              for _ in range(random_cases)]
    for pos, blocks, x, y in cases:
        initial = execution.CPU(rng)
        initial.sp = 0x1fe0
        state, page, table, src = 0x0600, 0x0800, 0x1000, 0x80fe
        # Four source bytes straddle a ROM page boundary. Zero-block runs must
        # not read source memory at all; partial runs must stop after one call.
        indices = rng.sample(range(256), 4)
        initial.mem[src:src+4] = bytes(indices)
        for j, index in enumerate(indices):
            initial.mem[table + index] = (y if j % 2 == 0 else -1-y) & 255
            initial.mem[page + ((pos + j) & 255)] = (x if j % 2 == 0 else -1-x) & 255
        initial.put('de', state)
        for off, value in [(0, page+pos), (2, src),
                           (4, table+rng.randrange(256)), (6, src+2*blocks)]:
            initial.write(f'({state+off})', value, 2)
        initial.writes.clear()
        expected = bytearray(initial.mem)
        for j in range(2*blocks):
            at = page + ((pos+j) & 255)
            lhs, rhs = expected[at], expected[table+indices[j]]
            lhs = lhs - 256 if lhs >= 128 else lhs
            rhs = rhs - 256 if rhs >= 128 else rhs
            expected[at] = max(-128, min(127, lhs+rhs)) & 255
        expected[state:state+2] = (page + ((pos+2*blocks) & 255)).to_bytes(2, 'little')
        expected[state+2:state+4] = (src+2*blocks).to_bytes(2, 'little')
        for name, program in programs.items():
            cpu = copy.deepcopy(initial)
            cpu.run(program, entry='pcm_run', untimed=exclusions[name])
            assert cpu.stop_reason == 'return' and cpu.calls == calls_per_block*blocks, (name, 'calls', cpu.calls, blocks)
            assert cpu.get('a') == blocks, (name, 'return value', cpu.get('a'), blocks)
            assert cpu.sp == initial.sp+2 and cpu.get('ix') == initial.get('ix'), (name, 'ABI')
            assert cpu.mem[page:page+256] == expected[page:page+256], (name, 'samples', pos, blocks, x, y)
            assert cpu.mem[state:state+8] == expected[state:state+8], (name, 'persistent cursors', pos, blocks)
            assert cpu.writes <= (set(range(page, page+256)) | set(range(state, state+4)) |
                                  set(range(initial.sp-256, initial.sp))), (name, 'stray write')
            assert sum(a >= 0x8000 for a in cpu.reads) == 2*blocks, (name, 'ROM reads')
            if name.endswith('_default'):
                assert cpu.alt == initial.alt, (name, 'reserved bank')
            times[name][blocks].append(cpu.cycles)
            inclusive[name][blocks].append(cpu.cycles + cpu.excluded_cycles)
    def metrics(values):
        return {str(n): {'cases': len(v), 'min_t_states': min(v),
                         'max_t_states': max(v), 'mean_t_states': statistics.mean(v)}
                for n, v in values.items()}
    return {'cases_per_implementation': len(cases),
            'comparison_by_completed_blocks': {name: metrics(v) for name, v in times.items()},
            'entry_exit_inclusive': {name: metrics(v) for name, v in inclusive.items()},
            'excluded_outer_instructions': {name: [programs[name].code[i] for i in sorted(indices)]
                                             for name, indices in exclusions.items()}}

