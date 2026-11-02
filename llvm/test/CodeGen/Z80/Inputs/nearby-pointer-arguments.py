"""Check nearby pointer arguments across calls and modular displacement wraps."""
import importlib.util
from pathlib import Path
import random
import sys
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
program = execution.Program(Path(sys.argv[1]).read_text())
rng = random.Random(1048)
checks = 0
for name, offsets in [('positive',(1024,1032,1040,1048)),
                      ('negative',(-1024,-1016,-1008,-1000)),
                      ('wrap',(32760,32767,-32762,-32755))]:
    for base in (0, 0x1234, 0x7ff0, 0xfff0):
        addresses = [(base+offset)&65535 for offset in offsets]
        for value in range(256):
            cpu = execution.CPU(rng)
            cpu.put('de',base)
            for i,address in enumerate(addresses): cpu.mem[address]=(value+i*37)&255
            sp,ix=cpu.sp,cpu.get('ix')
            cpu.run(program,entry=name)
            assert [a for a in cpu.reads if a in addresses]==addresses,(name,base,value)
            assert cpu.mem[8192]==(value+111)&255
            assert cpu.sp==sp+2 and cpu.get('ix')==ix
            checks+=1
print(f'{checks} nearby pointer argument executions passed')
