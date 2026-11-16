"""Check forward stream rebasing, partial groups and modular addresses."""
import importlib.util
from pathlib import Path
import random
import re
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)


class CPU(x.CPU):
    def __init__(self, seed, addresses):
        super().__init__(random.Random(seed))
        self.addresses = addresses
        self.trace = []

    def read(self, operand, width=1):
        value = super().read(operand, width)
        if operand.startswith('(') and self.addr(operand) in self.addresses:
            self.trace.append(('read', self.addr(operand), value))
        return value

    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) == 0x4000:
            self.trace.append(('write', 0x4000, value & 255))
        super().write(operand, value, width)


llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)
checks = 0
for allocator in ('z80', 'greedy'):
    for banks in ('-alternate-regs', '+alternate-regs'):
        for origin in ('last', 'first', 'middle'):
            for kind in ('complete', 'duplicate', 'reversed'):
                text = Path(source).read_text()
                offsets = (0, 1, 2, 3)
                if kind == 'duplicate':
                    text = text.replace('ptr %base, i16 -1', 'ptr %base, i16 -2')
                    offsets = (0, 1, 1, 3)
                elif kind == 'reversed':
                    text = text.replace('%cursor, -3', '%cursor, -2', 1)
                    text = text.replace('%address1 = add i16 %cursor, -2',
                                        '%address1 = add i16 %cursor, -3')
                    offsets = (1, 0, 2, 3)
                if origin != 'last':
                    shift = 3 if origin == 'first' else 2
                    text = text.replace('%source, 3', f'%source, {3-shift}')
                    text = re.sub(r'add i16 %cursor, (-?\d+)',
                                  lambda m: f'add i16 %cursor, {int(m[1])+shift}'
                                  if int(m[1]) != 4 else m[0], text)
                    text = text.replace('  %base = inttoptr i16 %cursor',
                                        f'  %original = add i16 %cursor, {shift}\n'
                                        '  %base = inttoptr i16 %original')
                    text = text.replace('%pointer3 = inttoptr i16 %cursor',
                                        '%pointer3 = inttoptr i16 %original')
                ir = directory / f'{allocator}{banks}-{kind}-{origin}.ll'
                ir.write_text(text)
                for enabled in (False, True):
                    flags = [llc, '-O2', '-disable-lsr', '-regalloc='+allocator,
                             '-mattr='+banks, '-verify-machineinstrs',
                             '-z80-rebase-forward-cursors='+str(enabled).lower(),
                             str(ir)]
                    prepared = subprocess.check_output(
                        [*flags, '-stop-before=irtranslator', '-o', '-'], text=True)
                    assert ('stream.next' in prepared) == (enabled and kind == 'complete')
                    assembly = subprocess.check_output([*flags, '-o', '-'], text=True)
                    program = x.Program(assembly)
                    for seed in range(16):
                        for base in (0, 0xffff, 0xfffe, 0x30fe):
                            for groups in (0, 1, 2, 7, 17):
                                for abort in (0, 1):
                                    reads = []
                                    for group in range(groups):
                                        for offset in offsets[:2] if abort else offsets:
                                            reads.append((base+4*group+offset)&65535)
                                        if abort:
                                            break
                                    cpu = CPU(seed, set(reads))
                                    expected = []
                                    for address in reads:
                                        value = cpu.mem[address]
                                        expected += [('read', address, value),
                                                     ('write', 0x4000, value)]
                                    result = base if not groups else base+2 if abort else base+4*(groups-1)+1
                                    cpu.put('de', base)
                                    cpu.put('c', groups)
                                    cpu.put('b', abort)
                                    sp, ix = cpu.sp, cpu.get('ix')
                                    cpu.run(program, entry='stream')
                                    context = (allocator, banks, kind, origin, enabled, seed, base, groups, abort)
                                    assert cpu.trace == expected, (context, cpu.trace, expected)
                                    assert cpu.get('hl') == result&65535, (context, cpu.get('hl'), result&65535)
                                    assert cpu.sp == sp+2 and cpu.get('ix') == ix
                                    checks += 1
print(f'{checks} forward stream executions passed')
