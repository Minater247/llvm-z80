"""Check advanced-address reuse, wraparound and intervening definitions."""
import argparse
import importlib.util
import random
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--llc', required=True)
parser.add_argument('--mir', type=Path, required=True)
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
args.work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        assembly = subprocess.check_output([
            args.llc, '-start-before=z80-machine-prera-opt',
            '-regalloc=' + allocator, '-verify-machineinstrs',
            '-mattr=' + ('+' if bank else '-') + 'alternate-regs',
            str(args.mir), '-o', '-'], text=True)
        (args.work / (allocator + str(bank) + '.s')).write_text(assembly)
        program = execution.Program(assembly)
        rng = random.Random(725)
        addresses = [0, 1, 254, 255, 256, 0x31fe, 0x31ff, 0xffff]
        addresses += [rng.randrange(0x1000, 0xd000) for _ in range(248)]
        for name in ('advanced', 'retreated', 'changed_base', 'changed_result',
                     'post_inc', 'observed_next', 'changed_old', 'live_old',
                     'duplicate_next', 'intervening_advanced'):
            for address in addresses:
                cpu = execution.CPU(rng)
                cpu.put('de', address)
                delta = -1 if name == 'retreated' else 1
                loaded = (address + delta + (name == 'changed_base')) & 65535
                result = (address + delta + (name == 'changed_result')) & 65535
                if name in ('post_inc', 'observed_next', 'changed_old',
                            'live_old', 'duplicate_next', 'intervening_advanced'):
                    loaded = (address + (name == 'changed_old')) & 65535
                expected = cpu.mem[loaded]
                advanced = cpu.mem[(address + 1) & 65535]
                sp, ix, alternate = cpu.sp, cpu.get('ix'), dict(cpu.alt)
                cpu.run(program, entry=name)
                assert cpu.get('a') == expected, (allocator, bank, name, address)
                assert cpu.get('de') == result, (allocator, bank, name, address)
                if name in ('observed_next', 'duplicate_next'):
                    assert cpu.get('bc') == result
                if name == 'intervening_advanced':
                    assert cpu.get('b') == advanced
                if name == 'live_old':
                    assert cpu.get('hl') == address
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                assert cpu.writes <= set(range(sp - 256, sp))
                if not bank:
                    assert cpu.alt == alternate
                checks += 1
print(f'{checks} advanced-cursor executions passed')
