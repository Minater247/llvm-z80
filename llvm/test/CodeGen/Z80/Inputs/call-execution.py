"""Exercise nested calls, real return addresses, timing boundaries and aborts."""
import importlib.util
import random
from pathlib import Path

spec = importlib.util.spec_from_file_location('execution', Path(__file__).with_name('z80-execution.py'))
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)
p = e.Program('''entry:
push ix
call middle
pop ix
ret
middle:
ld a, 3
call inner
inc a
ret
inner:
inc a
ret
''')
rng = random.Random(247)
for _ in range(32):
    cpu = e.CPU(rng)
    before_sp, before_ix = cpu.sp, cpu.get('ix')
    cpu.run(p, entry='entry', untimed={0, 2, 3})
    assert cpu.get('a') == 5 and cpu.get('ix') == before_ix
    assert cpu.cycles == 69 and cpu.excluded_cycles == 39
    assert cpu.sp == before_sp + 2 and cpu.calls == cpu.max_call_depth == 2
    assert cpu.writes <= set(range(before_sp-6, before_sp))
    assert cpu.stop_reason == 'return'

cpu = e.CPU(rng)
sp = cpu.sp
cpu.run('''entry:
call middle
ret
middle:
call panic
ret
panic:
ld a, 99
ret
''', entry='entry', aborts={'panic'})
assert cpu.stop_reason == 'abort:panic' and cpu.cycles == 34
assert cpu.sp == sp-4 and cpu.calls == 2

for program, message in [('call missing\nret', 'Unresolved call'),
                         ('call child\nret\nchild:\ninc sp\nret', 'Corrupted return address')]:
    cpu = e.CPU(rng)
    try:
        cpu.run(program)
    except AssertionError as error:
        assert message in str(error), error
    else:
        raise AssertionError('Invalid call/return accepted')
print('Checked nested calls, full inner timing, aborts and broken returns')
