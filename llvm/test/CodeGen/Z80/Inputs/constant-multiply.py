"""Check constant products, wraparound and live operands in generated code."""
import importlib.util
from pathlib import Path
import random
import sys

spec=importlib.util.spec_from_file_location('execution',Path(__file__).with_name('z80-execution.py'))
e=importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)
program=e.Program(Path(sys.argv[1]).read_text())
rng=random.Random(1703)
values=[0,1,2,255,256,4095,4096,8191,8192,9362,32767,32768,65534,65535]
values += [rng.randrange(65536) for _ in range(256)]
count=0
for x in values:
    for factor in [3,5,7,9,15,17]:
        cpu=e.CPU(rng);cpu.put('de',x)
        cpu.run(program,entry='mul'+str(factor))
        assert cpu.get('hl')==(x*factor)&65535,(x,factor,cpu.get('hl'))
        assert cpu.sp==0xf002
        count+=1
    y=rng.randrange(65536)
    cpu=e.CPU(rng);cpu.put('de',x);cpu.put('bc',y)
    cpu.run(program,entry='live_products')
    assert cpu.get('hl')==(17*x+3*y)&65535,(x,y,cpu.get('hl'))
    count+=1
    if x<=9362:
        cpu=e.CPU(rng);cpu.put('de',x)
        cpu.run(program,entry='nowrap7')
        assert cpu.get('hl')==7*x,(x,cpu.get('hl'))
        count+=1
for x in range(256):
    for factor in [3,5,7,9,15,17]:
        cpu=e.CPU(rng);cpu.put('e',x)
        cpu.run(program,entry='byte'+str(factor))
        assert cpu.get('a')==(x*factor)&255,(x,factor,cpu.get('a'))
        assert cpu.sp==0xf002
        count+=1
print('Checked',count,'constant products and live-operand cases')
