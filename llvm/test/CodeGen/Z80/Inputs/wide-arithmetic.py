"""Execute wide arithmetic, split calls, and independent flag consumers."""
import importlib.util
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location('execution', Path(__file__).with_name('z80-execution.py'))
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)
program = e.Program(Path(sys.argv[1]).read_text())
rng = random.Random(5427)
for i in range(512):
    a, b = rng.randrange(65536), rng.randrange(65536)
    for name, x, y, expected, result in [
        ('sub_shift', a, b, ((a-b)<<2)&65535, 'hl'),
        ('high_shift', a, b, a>>9, 'a'),
        ('roundtrip', a, b, ((a<<13)|(b>>7))>>16, 'hl'),
        ('two_conditions', i, b, 0 if i==0 else 2 if i==63 else 1, 'hl'),
        ('both_ranges', i%8, (i%4)*1024, 255 if i%8<6 and i%4<2 else 0, 'a'),
    ]:
        cpu=e.CPU(rng); cpu.put('de',x); cpu.put('bc',y)
        cpu.run(program,entry=name)
        assert cpu.get(result)==expected,(name,x,y,cpu.get(result),expected)
        assert cpu.sp==0xf002
    a,b=rng.randrange(1<<32),rng.randrange(1<<32)
    cpu=e.CPU(rng);cpu.put('de',a&65535);cpu.put('bc',a>>16);cpu.put('hl',b&65535)
    cpu.mem[cpu.sp+2]=(b>>16)&255;cpu.mem[cpu.sp+3]=b>>24
    cpu.run(program,entry='combine')
    actual=cpu.get('hl')|(cpu.get('de')<<16)
    assert actual==((a<<13)|(b>>7))&0xffffffff,(a,b,actual)
print('Checked 3072 arithmetic, ABI and condition cases')
