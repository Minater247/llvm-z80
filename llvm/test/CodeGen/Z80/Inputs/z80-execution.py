"""Execute Z80 assembly for compiler regression tests.

Unsupported instructions fail explicitly. The model counts instruction
T-states without bus waits or contention.
"""
import re


class Program:
    """Assembly labels and instruction indices; call returns use those indices.

    Instruction fetches are not data reads. Return addresses are checked on
    the emulated stack, so a broken frame cannot silently return successfully.
    """
    def __init__(self, body):
        self.code, self.labels = [], {}
        for raw in body.splitlines():
            line = raw.split(';', 1)[0].strip()
            if not line:
                continue
            if line.endswith(':'):
                label = line[:-1]
                assert label not in self.labels, ('Duplicate label', label)
                self.labels[label] = len(self.code)
            elif not line.startswith('.'):
                op, *tail = line.split(None, 1)
                self.code.append((op, [x.strip() for x in tail[0].split(',')] if tail else []))


class CPU:
    def __init__(self, rng):
        self.r = {x: rng.randrange(256) for x in ('a', 'f', 'b', 'c', 'd', 'e', 'h', 'l',
                                                'ixh', 'ixl', 'iyh', 'iyl')}
        self.alt = {x: rng.randrange(256) for x in 'afbcdehl'}
        self.sp = 0xf000
        self.mem = bytearray(rng.randbytes(65536))
        self.cycles = 0
        self.writes = set()
        self.reads = []

    def pair(self, reg):
        return ('ixh', 'ixl') if reg == 'ix' else ('iyh', 'iyl') if reg == 'iy' else tuple(reg)

    def get(self, reg):
        if reg == 'sp':
            return self.sp
        if reg in self.r:
            return self.r[reg]
        if reg in ('af', 'bc', 'de', 'hl', 'ix', 'iy'):
            hi, lo = self.pair(reg)
            return self.r[hi] * 256 + self.r[lo]
        return int(reg, 0)

    def put(self, reg, val):
        if reg == 'sp':
            self.sp = val & 65535
        elif reg in self.r:
            self.r[reg] = val & 255
        else:
            hi, lo = self.pair(reg)
            self.r[hi], self.r[lo] = (val >> 8) & 255, val & 255

    def addr(self, operand):
        parts = operand[1:-1].replace(' ', '')
        match = re.fullmatch(r'(ix|iy)([+-]\d+)?', parts)
        return ((self.get(match[1]) + int(match[2] or 0)) if match else self.get(parts)) & 65535

    def read(self, operand, width=1):
        if not operand.startswith('('):
            return self.get(operand)
        addr = self.addr(operand)
        self.reads.extend((addr + i) & 65535 for i in range(width))
        return sum(self.mem[(addr + i) & 65535] << (8 * i) for i in range(width))

    def write(self, operand, value, width=1):
        if not operand.startswith('('):
            self.put(operand, value)
            return
        addr = self.addr(operand)
        for i in range(width):
            at = (addr + i) & 65535
            self.mem[at] = (value >> (8 * i)) & 255
            self.writes.add(at)

    def instruction_complete(self):
        """Hook for bus traces, at the end of an executed instruction."""

    def run(self, body, entry=None, untimed=(), aborts=(),
            stop_on_reentry=False, max_steps=100000, checkpoints=None):
        """Execute until return, an explicit stop, or the instruction limit.

        Checkpoints map labels to callbacks receiving this CPU before the
        labeled instruction. A truthy callback result stops execution there.
        Untimed instructions apply only to the outer call; nested calls count.
        Exceeding max_steps or encountering an unsupported instruction raises.
        """
        program = Program(body) if isinstance(body, str) else body
        code, labels = program.code, program.labels
        pc = labels[entry] if entry else 0
        returns = []
        self.calls = 0
        self.max_call_depth = 0
        self.excluded_cycles = 0
        self.stop_reason = None
        abort_pcs = {labels[label]: label for label in aborts}
        checkpoint_pcs = {labels[label]: (label, callback)
                          for label, callback in (checkpoints or {}).items()}
        entry_pc = pc
        for step in range(max_steps):
            if step and stop_on_reentry and pc == entry_pc:
                self.stop_reason = 'reentry'
                return
            if pc in abort_pcs:
                self.stop_reason = 'abort:' + abort_pcs[pc]
                return
            if pc in checkpoint_pcs:
                label, callback = checkpoint_pcs[pc]
                if callback(self):
                    self.stop_reason = 'checkpoint:' + label
                    return
            before = self.cycles
            exclude = not returns and pc in untimed
            op, args = code[pc]
            pc += 1
            prefix = 4 if any(re.search(r'\b(?:ix|iy)', x) for x in args) else 0
            mem = next((x for x in args if x.startswith('(')), None)
            indexed = mem and re.match(r'\(i[xy]', mem)
            if op == 'ld':
                dst, src = args
                if dst == 'r':
                    assert src == 'a'
                    self.refresh = self.r['a']
                    self.cycles += 9
                    self.instruction_complete()
                    continue
                pair = (dst in ('bc', 'de', 'hl', 'ix', 'iy', 'sp') or
                        src in ('bc', 'de', 'hl', 'ix', 'iy', 'sp'))
                self.write(dst, self.read(src, 2 if pair else 1), 2 if pair else 1)
                if dst == 'sp':
                    self.cycles += 6 + prefix
                elif mem:
                    indirect = mem in ('(hl)', '(bc)', '(de)') or indexed
                    immediate = not src.startswith('(') and src not in self.r and not pair
                    if indirect:
                        self.cycles += (19 if indexed else 7) + (3 if immediate else 0)
                    else:
                        self.cycles += 16 if pair and ('hl' in args) else 20 if pair else 13
                elif src in self.r:
                    self.cycles += 4 + prefix
                else:
                    self.cycles += (10 if pair else 7) + prefix
            elif op in ('push', 'pop'):
                reg = args[0]
                if op == 'push':
                    self.sp = (self.sp - 2) & 65535
                    self.write('(sp)', self.get(reg), 2)
                else:
                    self.put(reg, self.read('(sp)', 2))
                    self.sp = (self.sp + 2) & 65535
                self.cycles += (11 if op == 'push' else 10) + prefix
            elif op == 'exx':
                for reg in 'bcdehl':
                    self.r[reg], self.alt[reg] = self.alt[reg], self.r[reg]
                self.cycles += 4
            elif op == 'ex':
                dst, src = args
                if src == "af'":
                    for reg in 'af':
                        self.r[reg], self.alt[reg] = self.alt[reg], self.r[reg]
                    self.cycles += 4
                else:
                    a, b = self.read(dst, 2), self.read(src, 2)
                    self.write(dst, b, 2)
                    self.write(src, a, 2)
                    self.cycles += (19 if mem else 4) + prefix
            elif op in ('add', 'adc', 'sub', 'sbc', 'cp'):
                dst, src = args if len(args) == 2 else ('a', args[0])
                a, b = self.get(dst), self.read(src)
                mask = 255 if dst == 'a' else 65535
                b &= mask
                carry = (self.r['f'] & 1) if op in ('adc', 'sbc') else 0
                subtract = op in ('sub', 'sbc', 'cp')
                value = a - b - carry if subtract else a + b + carry
                result = value & mask
                if op != 'cp':
                    self.put(dst, result)
                if dst != 'a':
                    assert op in ('add', 'adc', 'sbc'), (op, dst)
                    if op == 'add':
                        self.r['f'] = ((self.r['f'] & 0xc4) | ((result >> 8) & 0x28) |
                                       (0x10 if (a ^ b ^ result) & 0x1000 else 0) |
                                       (value > 65535))
                    else:
                        overflow = ((a ^ b) if subtract else ~(a ^ b)) & (a ^ result) & 0x8000
                        self.r['f'] = (((result >> 8) & 0xa8) | (0x40 if result == 0 else 0) |
                                       (0x10 if (a ^ b ^ result) & 0x1000 else 0) |
                                       (4 if overflow else 0) | (2 if subtract else 0) |
                                       (value < 0 or value > 65535))
                    self.cycles += (11 if op == 'add' else 15) + prefix
                else:
                    overflow = ((a ^ b) if subtract else ~(a ^ b)) & (a ^ result) & 0x80
                    self.r['f'] = ((result & 0xa8) | (0x40 if result == 0 else 0) |
                                   (0x10 if (a ^ b ^ result) & 0x10 else 0) |
                                   (4 if overflow else 0) | (2 if subtract else 0) |
                                   (value < 0 or value > 255))
                    self.cycles += 19 if indexed else 7 if mem or src not in self.r else 4 + prefix
            elif op in ('or', 'xor', 'and'):
                value = self.read(args[0]) & 255
                a = self.r['a']
                result = a | value if op == 'or' else a ^ value if op == 'xor' else a & value
                self.r['a'] = result
                self.r['f'] = ((result & 0xa8) | (0x40 if result == 0 else 0) |
                               (4 if result.bit_count() % 2 == 0 else 0) |
                               (0x10 if op == 'and' else 0))
                self.cycles += 19 if indexed else 7 if mem or args[0] not in self.r else 4 + prefix
            elif op in ('inc', 'dec'):
                reg = args[0]
                old = self.read(reg)
                val = old + (1 if op == 'inc' else -1)
                self.write(reg, val)
                if reg in self.r or mem:
                    result = val & 255
                    self.r['f'] = ((self.r['f'] & 1) | (result & 0xa8) |
                                   (0x40 if result == 0 else 0) |
                                   (0x10 if (old ^ result) & 0x10 else 0) |
                                   (4 if old == (0x7f if op == 'inc' else 0x80) else 0) |
                                   (2 if op == 'dec' else 0))
                    self.cycles += 23 if indexed else 11 if mem else 4 + prefix
                else:
                    self.cycles += 6 + prefix
            elif op in ('bit', 'set', 'res'):
                bit, dst = args
                bit = int(bit, 0)
                assert 0 <= bit < 8
                old = self.read(dst)
                if op == 'bit':
                    zero = not old & (1 << bit)
                    self.r['f'] = ((self.r['f'] & 1) | 0x10 | (old & 0x28) |
                                   (0x44 if zero else 0) |
                                   (0x80 if bit == 7 and not zero else 0))
                else:
                    self.write(dst, old | (1 << bit) if op == 'set' else old & ~(1 << bit))
                self.cycles += (20 if op == 'bit' else 23) if indexed else (12 if op == 'bit' else 15) if mem else 8
            elif op in ('rlca', 'rrca', 'rla', 'rra', 'rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'srl'):
                short = op in ('rlca', 'rrca', 'rla', 'rra')
                dst = 'a' if short else args[0]
                old = self.read(dst)
                carry = self.r['f'] & 1
                left = op in ('rlca', 'rla', 'rlc', 'rl', 'sla')
                out = (old >> 7) if left else old & 1
                value = ((old << 1) & 255) if left else old >> 1
                if op in ('rlca', 'rlc'): value |= out
                elif op in ('rrca', 'rrc'): value |= out << 7
                elif op in ('rla', 'rl'): value |= carry
                elif op in ('rra', 'rr'): value |= carry << 7
                elif op == 'sra': value |= old & 128
                self.write(dst, value)
                self.r['f'] = ((self.r['f'] & 0xc4) if short else
                               ((value & 128) | (0x40 if not value else 0) |
                                (4 if value.bit_count() % 2 == 0 else 0))) | (value & 0x28) | out
                self.cycles += 4 if short else 23 if indexed else 15 if mem else 8
            elif op == 'nop':
                self.cycles += 4
            elif op == 'neg':
                old = self.r['a']
                value = (-old) & 255
                self.r['a'] = value
                self.r['f'] = ((value & 0xa8) | (0x40 if value == 0 else 0) |
                               (0x10 if old & 15 else 0) |
                               (4 if old == 128 else 0) | 2 | bool(old))
                self.cycles += 8
            elif op in ('di', 'ei'):
                self.interrupts_enabled = op == 'ei'
                self.cycles += 4
            elif op == 'out':
                assert args[0] == '(c)', ('Unsupported output port', args)
                self.last_port_write = (self.get('bc'), self.get(args[1]))
                self.cycles += 12
            elif op == 'ldi':
                byte = self.read('(hl)')
                self.write('(de)', byte)
                self.put('hl', self.get('hl') + 1)
                self.put('de', self.get('de') + 1)
                self.put('bc', self.get('bc') - 1)
                total = (self.r['a'] + byte) & 255
                self.r['f'] = ((self.r['f'] & 0xc1) | (4 if self.get('bc') else 0) |
                               (total & 8) | ((total & 2) << 4))
                self.cycles += 16
            elif op == 'cpl':
                self.r['a'] ^= 255
                self.r['f'] = (self.r['f'] & 0xc5) | 0x12 | (self.r['a'] & 0x28)
                self.cycles += 4
            elif op in ('scf', 'ccf'):
                carry = self.r['f'] & 1
                self.r['f'] = ((self.r['f'] & 0xc4) | (self.r['a'] & 0x28) |
                               (1 if op == 'scf' else (carry << 4) | (carry ^ 1)))
                self.cycles += 4
            elif op == 'djnz':
                self.put('b', self.r['b'] - 1)
                if self.r['b']:
                    pc = labels[args[0]]
                self.cycles += 13 if self.r['b'] else 8
            elif op in ('jp', 'jr'):
                take = True
                if len(args) == 2:
                    cond = args[0]
                    take = {'z': bool(self.r['f'] & 64), 'nz': not self.r['f'] & 64,
                            'c': bool(self.r['f'] & 1), 'nc': not self.r['f'] & 1,
                            'pe': bool(self.r['f'] & 4), 'po': not self.r['f'] & 4,
                            'm': bool(self.r['f'] & 128), 'p': not self.r['f'] & 128}[cond]
                if take:
                    pc = labels[args[-1]]
                self.cycles += 10 if op == 'jp' else 12 if take else 7
            elif op == 'call':
                assert len(args) == 1, ('Unsupported conditional call', args)
                assert args[0] in labels, ('Unresolved call', args[0])
                self.sp = (self.sp - 2) & 65535
                self.write('(sp)', pc, 2)
                returns.append(pc)
                pc = labels[args[0]]
                self.calls += 1
                self.max_call_depth = max(self.max_call_depth, len(returns))
                self.cycles += 17
            elif op == 'ret':
                assert not args, ('Unsupported conditional return', args)
                self.cycles += 10
                if returns:
                    pc = self.read('(sp)', 2)
                    assert pc == returns.pop(), 'Corrupted return address'
                    self.sp = (self.sp + 2) & 65535
                else:
                    self.sp = (self.sp + 2) & 65535
                    self.stop_reason = 'return'
                    if exclude:
                        self.excluded_cycles += self.cycles - before
                        self.cycles = before
                    self.instruction_complete()
                    return
            else:
                raise AssertionError(f'Unsupported instruction: {op} {args}')
            if exclude:
                self.excluded_cycles += self.cycles - before
                self.cycles = before
            self.instruction_complete()
        raise AssertionError('Execution did not terminate')

