"""Execute private-state calls, external observations and page wrapping."""
import importlib.util
from pathlib import Path
import random
import re
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)


class CPU(execution.CPU):
    def __init__(self, rng):
        super().__init__(rng)
        self.samples = []

    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) == 8960:
            self.samples.append(value & 255)
        super().write(operand, value, width)


checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        for variant in ('ordinary', 'backward', 'word', 'disabled', 'volatile',
                        'escaped', 'unsupported', 'inline-leaf', 'barriers',
                        'noinline-call', 'multiple-callers', 'optnone-caller'):
            text = Path(source).read_text()
            if variant in ('inline-leaf', 'barriers', 'noinline-call',
                           'multiple-callers', 'optnone-caller'):
                text = text.replace('@advance() noinline', '@advance()')
                prefix, outside = text.split('define i16 @outside()', 1)
                if variant == 'barriers':
                    outside = outside.replace('  call fastcc void @advance()',
                        '  call void asm sideeffect "di", "~{memory}"()\n'
                        '  call fastcc void @advance()\n'
                        '  call void asm sideeffect "ei", "~{memory}"()')
                elif variant == 'noinline-call':
                    outside = outside.replace('call fastcc void @advance()',
                                              'call fastcc void @advance() noinline')
                elif variant == 'multiple-callers':
                    outside += '\ndefine i16 @another() ' + outside
                elif variant == 'optnone-caller':
                    outside = ' noinline optnone' + outside
                text = prefix + 'define i16 @outside()' + outside
            elif variant == 'backward':
                text = text.replace('add i16 %p, 1', 'add i16 %p, -1')
            elif variant == 'word':
                text = text.replace('%next = or i16 %hi, %lo',
                                    '%next = add i16 %p, 1')
            elif variant == 'volatile':
                text = text.replace('load i16, ptr @cursor',
                                    'load volatile i16, ptr @cursor')
            elif variant == 'escaped':
                text += '\n@escape = global ptr @cursor\n'
            elif variant == 'unsupported':
                # A word mask needs primary byte arithmetic; retain memory.
                text = text.replace('%next = or i16 %hi, %lo',
                                    '%next = and i16 %n, 16383')
            # Name changes must not affect discovery or allocation.
            text = text.replace('@cursor', '@private_word')
            ir = directory / f'{allocator}-{bank}-{variant}.ll'
            ir.write_text(text)
            path = ir.with_suffix('.s')
            subprocess.run([llc, '-z80-promote-private-state='+str(variant != 'disabled').lower(),
                            '-mattr='+('+' if bank else '-')+'alternate-regs',
                            '-regalloc='+allocator, '-verify-machineinstrs',
                            str(ir), '-o', str(path)], check=True)
            assembly = path.read_text()
            promoted = bank and variant not in ('disabled', 'volatile', 'escaped', 'unsupported', 'optnone-caller')
            assert ('advance.state:' in assembly) == promoted
            inlined = bank and variant in ('inline-leaf', 'barriers')
            assert ('\nadvance:' not in assembly) == inlined
            if inlined:
                body = assembly.split('\noutside:', 1)[1].split('.Lfunc_end', 1)[0]
                assert 'call' not in body and 'cursor' not in body
                assert 'private_word' in body
                if variant == 'barriers':
                    assert body.index('di') < body.index('private_word') < body.index('ei')
            assembly = re.sub(r'\bprivate_word\b', '8192', assembly)
            # The observer checks flushed memory, changes it, and destroys
            # volatile alternate registers, as an external callee may do.
            assembly += '''
observe:
ld hl, (8192)
ld (8194), hl
inc l
ld (8192), hl
exx
ld hl, 1234
ld de, 5678
ld bc, 9012
exx
ret
_setflag:
push hl
push af
pop hl
ld a, l
xor 128
ld l, a
push hl
pop af
pop hl
ret
'''
            program = execution.Program(assembly)
            rng = random.Random(247)
            for start in range(256):
                cpu = CPU(rng)
                cursor = 0x3100 | start
                cpu.mem[8192:8194] = cursor.to_bytes(2, 'little')
                samples = []

                def step():
                    global cursor
                    samples.append(cpu.mem[cursor])
                    delta = -1 if variant == 'backward' else 1
                    cursor = ((cursor+1) & 65535 if variant == 'word'
                              else (cursor+1) & 16383 if variant == 'unsupported'
                              else (cursor & 0xff00) | ((cursor+delta) & 255))

                for _ in range(4):
                    step()
                observed = cursor
                cursor = (cursor & 0xff00) | ((cursor+1) & 255)
                signed = lambda n: (n ^ 128) - 128
                extra = signed(cpu.mem[9216]) < signed(cpu.mem[9217])
                for _ in range(4 if extra else 0):
                    step()
                ix = cpu.get('ix')
                cpu.run(program, entry='run')
                assert cpu.samples == samples, (ir, start, cpu.samples, samples)
                assert int.from_bytes(cpu.mem[8192:8194], 'little') == cursor
                assert int.from_bytes(cpu.mem[8194:8196], 'little') == observed
                assert cpu.sp == 0xf002 and cpu.get('ix') == ix
                cpu.sp = 0xf000
                step()
                cpu.run(program, entry='outside')
                assert cpu.get('hl') == cursor
                assert cpu.samples == samples
                checks += 1
print(f'Checked {checks} private-state executions')
