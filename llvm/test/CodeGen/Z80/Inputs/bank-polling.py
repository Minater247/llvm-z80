"""Compare banked polling with individual bank operations and delayed readiness."""
import argparse
import copy
import importlib.util
from pathlib import Path
import random
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument('--llc', required=True)
ap.add_argument('--work', type=Path, required=True)
args = ap.parse_args()
args.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)


class PollCPU(x.CPU):
    def __init__(self, seed, delay):
        super().__init__(random.Random(seed))
        self.delay = delay
        self.polls = 0
        self.trace = []
        for pair, value in [('hl', 0x4000), ('de', 0x4001), ('bc', 0x50ff)]:
            self.alt[pair[0]], self.alt[pair[1]] = value >> 8, value & 255

    def read(self, operand, width=1):
        if operand.startswith('(') and self.addr(operand) == 0x4000:
            self.polls += 1
            value = int(self.polls > self.delay)
            self.trace.append(('read', 0x4000, value))
            return value
        return super().read(operand, width)

    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) in (0x4000, 0x4001):
            self.trace.append(('write', self.addr(operand), value & 255))
        super().write(operand, value, width)


live = ['$af', '$bc', '$de', '$hl', '$ix', '$iy', '$bc_alt', '$de_alt', '$hl_alt']
# A must remain available to the loop when it is used there. A primary-bank
# counter also prevents moving the bank activation around the whole backedge.
cases = [('', '$a = LD8gg $b', True),
         ('', '$a = LD8ri 57', True),
         ('OR8ai 8, implicit-def $a, implicit-def $f, implicit $a',
          '$a = LD8gg $c', False),
         ('$b = INC8r $b, implicit-def $f', '$a = LD8gg $b', False)]
mir = '--- |\n  target triple = "z80"\n'
for index in range(len(cases)):
    mir += f'  define void @case{index}() nounwind {{ ret void }}\n'
mir += '...\n'
for index, (prefix, copy_in, _) in enumerate(cases):
    mir += f'''---
name: case{index}
tracksRegLiveness: true
body: |
  bb.0:
    successors: %bb.1
    liveins: {', '.join(live)}
    JQ %bb.1
  bb.1:
    successors: %bb.1, %bb.2
    liveins: {', '.join(live)}
    {prefix}
    BANK_BIT8pb $hl_alt, 0, implicit-def $f
    JQCC %bb.1, 1, implicit $f
    JQ %bb.2
  bb.2:
    liveins: {', '.join(live)}
    {copy_in}
    BANK_LD8pi $hl_alt, 39
    BANK_LD8pa $de_alt, $a
    $a = BANK_LD8ap $bc_alt
    BANK_LD8pa $de_alt, $a
    $bc_alt = BANK_INCL16 $bc_alt, implicit-def $f
    RET16 implicit-def $sps, implicit $sps, {', '.join('implicit '+r for r in live)}
...
'''
source = args.work / 'polling.mir'
source.write_text(mir)
programs = []
for mode in ('after', 'before'):
    assembly = subprocess.check_output([
        args.llc, '-mattr=+alternate-regs', '-verify-machineinstrs',
        '-start-'+mode+'=z80-bank-regions', str(source), '-o', '-'], text=True)
    (args.work / (mode+'.s')).write_text(assembly)
    programs.append(x.Program(assembly))
checks = 0
for index, (_, _, fused) in enumerate(cases):
    for delay in (0, 1, 2, 7, 31):
        for seed in range(16):
            baseline = PollCPU(seed, delay)
            candidate = copy.deepcopy(baseline)
            baseline.run(programs[0], entry=f'case{index}')
            candidate.run(programs[1], entry=f'case{index}')
            assert baseline.trace == candidate.trace, (index, delay, seed)
            assert candidate.polls == delay + 1
            assert baseline.r == candidate.r, (index, delay, seed, 'registers')
            assert baseline.alt == candidate.alt, (index, delay, seed, 'bank')
            assert baseline.mem == candidate.mem
            assert baseline.sp == candidate.sp
            if fused:
                assert candidate.cycles <= baseline.cycles - 8 * (delay+1)
            checks += 1
print(f'{checks} bank polling executions passed')
