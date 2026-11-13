"""Execute compound byte conditions and verify their control-flow lowering."""
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
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        out = directory / f'{allocator}-{bank}.s'
        flags = [llc, '-regalloc='+allocator,
                 '-mattr='+('+' if bank else '-')+'alternate-regs',
                 '-verify-machineinstrs', source]
        subprocess.run([*flags, '-o', str(out)], check=True)
        if allocator == 'z80' and not bank:
            ir = out.with_suffix('.mir')
            subprocess.run([*flags, '-stop-before=irtranslator', '-o', str(ir)], check=True)
            text = ir.read_text()
            for name in ('either', 'both', 'nested'):
                body = text.split('@'+name+'(', 1)[1].split('\n  }', 1)[0]
                assert 'cond.split' in body, (name, body)
            # This case retains a shared Boolean result instead of moving it.
            body = text.split('@shared(', 1)[1].split('\n  }', 1)[0]
            assert 'cond.split' not in body
        program = x.Program(out.read_text())
        rng = random.Random(914)
        pairs = [(a, b) for a in range(256)
                 for b in (0, 17, 36, 37, 71, 72, 96, 97, 127, 128, 193, 255)]
        pairs += [(a, b) for b in range(256)
                  for a in (0, 17, 36, 37, 96, 97, 127, 128, 255)]
        for a, b in pairs:
            expected = {'either': ((a ^ 85) if a < 37 or b == 193 else b+11) & 255,
                        'both': a if a >= 97 and b >= 128 else b,
                        'nested': 23 if (a == 17 or b < 72) and a >= 128 else 41,
                        'shared': a if a == 17 and b < 72 else int(b < 72)}
            for name, result in expected.items():
                cpu = x.CPU(rng)
                cpu.put('e', a)
                cpu.put('d', b)
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                assert cpu.get('a') == result, (allocator, bank, name, a, b, cpu.get('a'), result)
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} compound-branch executions passed')
