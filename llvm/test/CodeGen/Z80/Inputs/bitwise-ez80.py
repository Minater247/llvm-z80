"""Execute emitted ADL bitwise code, including upper bytes and real stack slots.

The model implements the straight-line instruction subset used by these tests;
an unsupported instruction fails explicitly. Pair operations and stack entries
use 24 bits, while byte-register writes preserve the other two register lanes.
"""
import importlib.util
from pathlib import Path
import random
import re
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
MASK = 0xffffff
PAIRS = {'bc': ('c', 'b', 'bcu'), 'de': ('e', 'd', 'deu'),
         'hl': ('l', 'h', 'hlu'), 'ix': ('ixl', 'ixh', 'ixu'),
         'iy': ('iyl', 'iyh', 'iyu')}


class CPU:
    def __init__(self, rng):
        self.reg = {r: rng.randrange(256) for lanes in PAIRS.values() for r in lanes}
        self.reg.update(a=rng.randrange(256), f=rng.randrange(256))
        self.alt = {r: rng.randrange(256) for r in self.reg}
        self.sp = 0x700000
        self.mem = {}
        self.writes = set()

    def get(self, r):
        if r == 'sp':
            return self.sp
        if r in self.reg:
            return self.reg[r]
        if r in PAIRS:
            return sum(self.reg[b] << (8 * i) for i, b in enumerate(PAIRS[r]))
        return int(r, 0)

    def put(self, r, value):
        if r == 'sp':
            self.sp = value & MASK
        elif r in self.reg:
            self.reg[r] = value & 255
        else:
            for i, b in enumerate(PAIRS[r]):
                self.reg[b] = (value >> (8 * i)) & 255

    def address(self, operand):
        text = operand[1:-1].replace(' ', '')
        match = re.fullmatch(r'(bc|de|hl|ix|iy|sp)([+-]\d+)?', text)
        return ((self.get(match[1]) + int(match[2] or 0))
                if match else int(text, 0)) & MASK

    def load(self, address, width):
        return sum(self.mem.get((address + i) & MASK, 0xa7) << (8 * i)
                   for i in range(width))

    def store(self, address, value, width):
        for i in range(width):
            at = (address + i) & MASK
            self.mem[at] = (value >> (8 * i)) & 255
            self.writes.add(at)

    def run(self, name):
        pc = program.labels[name]
        for _ in range(1000):
            op, args = program.code[pc]
            pc += 1
            if op == 'ld':
                dst, src = args
                if dst.startswith('('):
                    self.store(self.address(dst), self.get(src), 3 if src in PAIRS else 1)
                elif src.startswith('('):
                    self.put(dst, self.load(self.address(src), 3 if dst in PAIRS else 1))
                else:
                    self.put(dst, self.get(src))
            elif op == 'lea':
                self.put(args[0], self.address('(' + args[1] + ')'))
            elif op in ('and', 'or', 'xor'):
                src = args[-1]
                value = self.load(self.address(src), 1) if src.startswith('(') else self.get(src)
                a = self.get('a')
                self.put('a', a & value if op == 'and' else a | value if op == 'or' else a ^ value)
                self.reg['f'] = 0
            elif op in ('add', 'sbc'):
                dst, src = args
                a, b = self.get(dst), self.get(src)
                limit = MASK if dst in PAIRS or dst == 'sp' else 255
                result = a + b if op == 'add' else a - b - (self.reg['f'] & 1)
                self.put(dst, result)
                self.reg['f'] = int(result < 0 or result > limit)
            elif op == 'scf':
                self.reg['f'] |= 1
            elif op == 'cpl':
                self.put('a', ~self.get('a'))
            elif op in ('inc', 'dec'):
                self.put(args[0], self.get(args[0]) + (1 if op == 'inc' else -1))
            elif op == 'push':
                assert args[0] in PAIRS, args
                self.sp -= 3
                self.store(self.sp, self.get(args[0]), 3)
            elif op == 'pop':
                assert args[0] in PAIRS, args
                self.put(args[0], self.load(self.sp, 3))
                self.sp += 3
            elif op == 'ex' and args == ['de', 'hl']:
                de, hl = self.get('de'), self.get('hl')
                self.put('de', hl)
                self.put('hl', de)
            elif op == 'exx':
                for pair in ('bc', 'de', 'hl'):
                    for r in PAIRS[pair]:
                        self.reg[r], self.alt[r] = self.alt[r], self.reg[r]
            elif op == 'ex' and args == ['af', "af'"]:
                for r in ('a', 'f'):
                    self.reg[r], self.alt[r] = self.alt[r], self.reg[r]
            elif op == 'ret':
                assert self.load(self.sp, 3) == 0x654321, 'Corrupted return address'
                self.sp += 3
                return
            else:
                raise AssertionError((op, args))
        raise AssertionError('Execution did not terminate')


rng = random.Random(24083)
edges = [0, 1, 255, 256, 65535, 65536, 0x7fffff, 0x800000, MASK]
inputs = [(x, y) for x in edges for y in edges]
inputs += [(rng.randrange(1 << 24), rng.randrange(1 << 24)) for _ in range(1024)]
checks = 0
for x, y in inputs:
    for name, expected in [('and24', x & y), ('or24', x | y),
                           ('xor24', x ^ y), ('not24', x ^ MASK),
                           ('mask_pointer', x & y)]:
        cpu = CPU(rng)
        sp, ix = cpu.sp, cpu.get('ix')
        cpu.store(sp, 0x654321, 3)
        cpu.store(sp + 3, x, 3)
        cpu.store(sp + 6, y, 3)
        cpu.writes.clear()
        cpu.run(name)
        assert cpu.get('hl') == expected, (name, x, y, cpu.get('hl'), expected)
        assert cpu.sp == sp + 3 and cpu.get('ix') == ix
        assert cpu.writes <= set(range(sp - 128, sp))
        checks += 1
print(f'{checks} complete ADL bitwise executions passed')
