"""Exercise complementary byte fields and observable source/mask lifetimes."""
import importlib.util
from pathlib import Path
import random
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)
masks = (1, 3, 15, 85, 128, 129, 170, 240, 254)
text = Path(source).read_text()
for mask in masks:
    text += f'''
define i8 @mask_{mask}(i8 %old, i8 %new) nounwind {{
  %x = and i8 %old, {mask}
  %y = and i8 %new, {mask^255}
  %r = or i8 %x, %y
  ret i8 %r
}}
'''
module = directory/'merges.ll'
module.write_text(text)
checks = 0

def check(program, name, a, b, expected):
    global checks
    cpu = x.CPU(random.Random(a*257+b))
    cpu.put('e', a)
    cpu.put('d', b)
    sp, ix = cpu.sp, cpu.get('ix')
    cpu.run(program, entry=name)
    assert cpu.get('a') == expected, (name, a, b, cpu.get('a'), expected)
    if name == 'preserved':
        assert cpu.mem[0x5000:0x5002] == bytes((a, b))
    if name == 'shared_mask':
        assert cpu.mem[0x5000] == a&15
    assert cpu.sp == sp+2 and cpu.get('ix') == ix
    checks += 1

for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        assembly = subprocess.check_output([
            llc, '-regalloc='+allocator, '-verify-machineinstrs',
            '-mattr='+('+' if bank else '-')+'alternate-regs',
            str(module), '-o', '-'], text=True)
        (directory/f'{allocator}-{bank}.s').write_text(assembly)
        program = x.Program(assembly)
        for a in range(256):
            for b in (0, 255, (a*73+19)&255):
                for mask in masks:
                    check(program, f'mask_{mask}', a, b,
                          (a&mask)|(b&(mask^255)))
                for name in ('preserved', 'shared_mask', 'overlapping'):
                    check(program, name, a, b,
                          (a&15)|(b&(31 if name=='overlapping' else 240)))
        if allocator == 'z80' and bank:
            for a in range(256):
                for b in range(256):
                    check(program, 'nibbles', a, b, (a&15)|(b&240))
print(f'{checks} byte-field merge executions passed')
