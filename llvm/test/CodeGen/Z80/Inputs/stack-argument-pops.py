"""Check frameless stack arguments, including interrupts between stack updates."""
import argparse
import importlib.util
import random
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--llc', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(exist_ok=True)
    spec = importlib.util.spec_from_file_location(
        'execution', Path(__file__).with_name('z80-execution.py'))
    execution = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(execution)

    class InterruptCPU(execution.CPU):
        """Inject a register-preserving interrupt after a chosen instruction."""
        def instruction_complete(self):
            self.instructions += 1
            if self.instructions != self.interrupt_at:
                return
            registers, sp = dict(self.r), self.sp
            # The handler saves a PC and all primary registers on the current
            # stack, clobbers them, and restores them before returning.
            saved = [0x1234] + [self.get(r) for r in ('af', 'bc', 'de', 'hl', 'ix', 'iy')]
            for value in saved:
                self.sp -= 2
                self.write('(sp)', value, 2)
            self.r = {reg: value ^ 0xff for reg, value in self.r.items()}
            for value in reversed(saved):
                assert self.read('(sp)', 2) == value
                self.sp += 2
            self.r = registers
            assert self.sp == sp

    pairs = ('bc', 'de', 'hl', 'iy')
    mir = '--- |\n  target triple = "z80"\n'
    for pair in pairs:
        mir += f'  define void @word_{pair}() nounwind {{ ret void }}\n'
    mir += '...\n'
    for pair in pairs:
        mir += f'''---
name: word_{pair}
tracksRegLiveness: true
fixedStack:
  - {{ id: 0, offset: 0, size: 2, alignment: 1, isImmutable: true }}
body: |
  bb.0:
    ${pair} = LD88ro %fixed-stack.0, 0 :: (load (s16) from %fixed-stack.0)
    $hl = COPY ${pair}
    RET16 implicit-def $sps, implicit $sps, implicit $hl
...
'''
    source = args.work / 'loads.mir'
    source.write_text(mir)
    cases = 0
    for bank in (False, True):
        assembly = subprocess.check_output([
            args.llc, '-start-before=prologepilog', '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(source), '-o', '-'], text=True)
        (args.work / ('bank.s' if bank else 'default.s')).write_text(assembly)
        program = execution.Program(assembly)
        rng = random.Random(441)
        for pair in pairs:
            start = program.labels['word_' + pair]
            assert program.code[start:start+4] == [
                ('pop', ['af']), ('pop', [pair]),
                ('push', [pair]), ('push', ['af'])]
            for value in range(256):
                for interrupt_at in range(5):
                    cpu = InterruptCPU(rng)
                    cpu.instructions, cpu.interrupt_at = 0, interrupt_at
                    cpu.sp = sp = 0x9000 + value * 4
                    # Every possible AF low byte must preserve its exact bits
                    # while temporarily holding the caller's return address.
                    cpu.mem[sp:sp+4] = bytes((value, value ^ 0x5a, value ^ 0xa5, value))
                    expected = cpu.mem[sp+2] | cpu.mem[sp+3] << 8
                    memory, alt = bytes(cpu.mem), dict(cpu.alt)
                    ix = cpu.get('ix')
                    cpu.run(program, entry='word_' + pair)
                    assert cpu.get('hl') == expected
                    assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                    assert cpu.alt == alt
                    assert cpu.mem[:sp-32] == memory[:sp-32]
                    assert cpu.mem[sp:] == memory[sp:]
                    assert cpu.writes <= set(range(sp-32, sp+4))
                    cases += 1
    print(f'{cases} stack-argument executions passed')


if __name__ == '__main__':
    main()
