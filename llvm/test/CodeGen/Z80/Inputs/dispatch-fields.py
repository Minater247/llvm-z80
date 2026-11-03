"""Execute all byte values through shared and distinct switch destinations."""
import importlib.util
from pathlib import Path
import random
import re
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
        for enabled in (False, True):
            out = directory / f'{allocator}-{bank}-{enabled}.s'
            flags = [llc, '-regalloc='+allocator,
                     '-mattr='+('+' if bank else '-')+'alternate-regs',
                     '-z80-prepare-dispatch='+str(enabled).lower(),
                     '-verify-machineinstrs', source]
            subprocess.run([*flags, '-o', str(out)], check=True)
            if allocator == 'z80' and not bank:
                ir = out.with_suffix('.mir')
                subprocess.run([*flags, '-stop-before=irtranslator', '-o', str(ir)], check=True)
                assert ('dispatch.field' in ir.read_text()) == enabled
            assembly = out.read_text()
            tables = {}
            for line in assembly.splitlines():
                line = line.split(';', 1)[0].strip()
                if re.fullmatch(r'\.LJTI\d+_\d+:', line):
                    table = line[:-1]
                    tables[table] = []
                elif re.match(r'(?:dw|d16)\s', line):
                    tables[table].append(line.split()[1])
            addresses = {name: 0x8000 + i * 512 for i, name in enumerate(tables)}
            body = re.sub(r'^\s*(?:dw|d16)\s+.*$', '', assembly, flags=re.M)
            body = re.sub(r'(\bld\s+\w+,\s*)(\.LJTI\d+_\d+)',
                          lambda m: m[1]+str(addresses[m[2]]), body)
            program = x.Program(body)
            for command in range(256):
                cpu = x.CPU(random.Random(command))
                for name, entries in tables.items():
                    for i, target in enumerate(entries):
                        at = addresses[name] + i * 2
                        cpu.mem[at:at+2] = program.labels[target].to_bytes(2, 'little')
                cpu.put('e', command)
                sp, ix = cpu.sp, cpu.get('ix')
                cpu.run(program, entry='dispatch')
                group, low, port = command >> 5, command & 3, (command >> 2) & 1
                expected = [command+low, command+low, command-low,
                            command^low, command+port, command-port,
                            command^port, command][group] & 255
                assert cpu.get('a') == expected, (allocator, bank, enabled, command)
                assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                checks += 1
print(f'{checks} dispatch executions passed')
