"""Check relative-branch limits across pseudos expanded at assembly emission."""
from pathlib import Path
import re
import subprocess
import sys

llc, temporary = sys.argv[1:]
# Transfer lengths follow their architectural sequences: EXX/PUSH/EXX/POP
# for a pair, an extra index prefix for IY, and a single EX AF,AF' exchange.
transfers = [
    ('$hl_alt = ALT_SAVE16 $hl', 4, '$hl'),
    ('$hl_alt = ALT_SAVE16 $iy', 5, '$iy'),
    ('$a_alt = ALT_SWAP_SAVE8 $a, implicit-def $a, implicit-def $f, implicit-def $f_alt', 1, '$a'),
]
checks = 0
for number, (operation, size, livein) in enumerate(transfers):
    for backwards in (False, True):
        for displacement in (126, 127, 128, 129):
            # Backward displacements include the proposed two-byte JR itself.
            body_size = displacement - (2 if backwards else 0)
            copies, remainder = divmod(body_size, size)
            instructions = ('    ' + operation + '\n') * copies
            instructions += '    NOP\n' * remainder
            if backwards:
                body = ('  bb.0:\n    successors: %bb.0\n'
                        '    liveins: ' + livein + '\n' + instructions +
                        '    JQ %bb.0\n')
            else:
                body = ('  bb.0:\n    successors: %bb.2\n    JQ %bb.2\n'
                        '  bb.1:\n    successors: %bb.2\n    liveins: ' + livein + '\n' +
                        instructions + '  bb.2:\n    RET16 implicit-def $sps, implicit $sps\n')
            source = Path(temporary + f'-{number}-{backwards}-{displacement}.mir')
            source.write_text('--- |\n  target triple = "z80"\n'
                              '  define void @test() { ret void }\n...\n---\n'
                              'name: test\ntracksRegLiveness: true\nbody: |\n' + body + '...\n')
            result = subprocess.check_output(
                [llc, '-mattr=+alternate-regs', '-run-pass=z80-branch-select',
                 '-verify-machineinstrs', str(source), '-o', '-'], text=True)
            expected = 'JR' if displacement <= (128 if backwards else 127) else 'JP16'
            assert re.search(r'^\s+' + expected + r' %bb\.', result, re.M), (
                number, backwards, displacement, expected, result)
            checks += 1
print(f'Checked {checks} emission-pseudo branch boundaries')
