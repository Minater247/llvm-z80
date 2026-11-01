"""Check every constant right shift of a 24-bit value against integer arithmetic."""
import importlib.util
from pathlib import Path
import random
import sys

if sys.argv[1] == '--emit':
    source = 'target triple = "ez80"\n'
    for op in ('lshr', 'ashr'):
        for count in range(24):
            source += (f'define i24 @{op}{count}(i24 %x) {{\n'
                       f'  %r = {op} i24 %x, {count}\n  ret i24 %r\n}}\n')
    Path(sys.argv[2]).write_text(source)
    sys.exit()


def module(name):
    spec = importlib.util.spec_from_file_location(
        name, Path(__file__).with_name(name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


execution, adl = module('z80-execution'), module('ez80-execution')
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(2416)
values = [0, 1, 127, 128, 255, 256, 65535, 65536, 0x7fffff, 0x800000, 0xffffff]
values += [rng.randrange(1 << 24) for _ in range(1024)]
checks = 0
for value in values:
    for op in ('lshr', 'ashr'):
        signed = value - (1 << 24) if op == 'ashr' and value & (1 << 23) else value
        for count in range(24):
            cpu = adl.CPU(rng)
            sp, ix = cpu.sp, cpu.get('ix')
            cpu.store(sp, 0x654321, 3)
            cpu.store(sp + 3, value, 3)
            cpu.writes.clear()
            name = f'{op}{count}'
            cpu.run(program, name)
            expected = (signed >> count) & adl.MASK
            assert cpu.get('hl') == expected, (name, value, cpu.get('hl'), expected)
            assert cpu.sp == sp + 3 and cpu.get('ix') == ix
            assert cpu.writes <= set(range(sp - 128, sp))
            checks += 1
print(f'{checks} ADL right-shift executions passed')
