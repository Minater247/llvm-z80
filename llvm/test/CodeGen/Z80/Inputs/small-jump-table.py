"""Check every valid table offset, including the byte-to-word boundary."""
import importlib.util
from pathlib import Path
import random
import re
import sys

spec = importlib.util.spec_from_file_location(
    'execution', Path(__file__).with_name('z80-execution.py'))
x = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x)
assembly = Path(sys.argv[1]).read_text()
tables = {}
for line in assembly.splitlines():
    line = line.split(';', 1)[0].strip()
    if re.fullmatch(r'\.LJTI\d+_\d+:', line):
        table = line[:-1]
        tables[table] = []
    elif re.match(r'(?:dw|d16)\s', line):
        tables[table].append(line.split()[1])

checks = 0
for base in (0x8000, 0xff80):
    # Stop at the indirect jump and expose its loaded destination. The helper
    # executes instruction indices, so table entries use distinct sentinel
    # addresses rather than addresses of the test program's instructions.
    body = re.sub(r'^\s*(?:dw|d16)\s+.*$', '', assembly, flags=re.M)
    body = re.sub(r'\bjp\s+\((hl|ix|iy)\)',
                  r'ld (16384), \1\n\tret', body)
    body = re.sub(r'(\bld\s+\w+,\s*)\.LJTI\d+_\d+',
                  lambda m: m[1] + str(base), body)
    program = x.Program(body)
    for number, (name, count) in enumerate([
            ('byte_index', 4), ('word_index', 4),
            ('last_byte_offset', 128), ('wide_offset', 129)]):
        entries = tables[f'.LJTI{number}_0']
        assert len(entries) == count
        targets = {label: 0x6100 + i * 17
                   for i, label in enumerate(sorted(set(entries)))}
        for index, label in enumerate(entries):
            cpu = x.CPU(random.Random(index))
            for offset, destination in enumerate(entries):
                value = targets[destination]
                cpu.mem[(base + offset * 2) & 65535] = value & 255
                cpu.mem[(base + offset * 2 + 1) & 65535] = value >> 8
            cpu.put('bc', index | (0xa500 if name == 'byte_index' else 0))
            cpu.run(program, entry=name)
            result = cpu.mem[16384] | cpu.mem[16385] << 8
            assert result == targets[label], (name, index, result, targets[label])
            assert cpu.sp == 0xf002
            checks += 1
print('Checked', checks, 'jump-table destinations')
