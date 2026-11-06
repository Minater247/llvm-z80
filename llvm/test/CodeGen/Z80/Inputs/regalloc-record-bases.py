"""Check field values, branches, pointer wrapping and call preservation."""

import importlib.util
import random
import subprocess
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "execution", Path(__file__).with_name("z80-execution.py")
)
execution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(execution)
llc, source, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(exist_ok=True)
stub = """
opaque:
 ld bc, 499
 ld de, 986
 ld hl, 1235
 ld iy, 2345
 exx
 ld bc, 3456
 ld de, 4567
 ld hl, 5678
 exx
 ld a, 66
 ex af, af'
 ld a, 99
 ret
"""

for allocator in ("z80", "greedy"):
    for bank in ("+", "-"):
        asm = subprocess.check_output(
            [llc, "-regalloc=" + allocator, "-mattr=" + bank + "alternate-regs",
             "-verify-machineinstrs", source, "-o", "-"], text=True
        )
        (directory / f"{allocator}-{bank}.s").write_text(asm)
        program = execution.Program(asm + stub)
        for base in (0, 512, 32700, 65000, 65535):
            for mode in (0, 1, 255):
                for seed in range(16):
                    cpu = execution.CPU(random.Random(seed))
                    cpu.put("de", base)
                    cpu.put("c", mode)
                    addresses = [(base + i) & 65535 for i in (300, 301, 303, 305)]
                    original = [cpu.mem[a] for a in addresses]
                    expected = original[:]
                    if mode:
                        expected[1] = original[2]
                        expected[2] = original[1]
                    else:
                        expected[0] = original[3]
                        expected[1] = original[0]
                    expected[3] = original[1]
                    ix, sp = cpu.get("ix"), cpu.sp
                    cpu.run(program, entry="record")
                    assert [cpu.mem[a] for a in addresses] == expected, (
                        allocator, bank, base, mode, seed
                    )
                    assert cpu.get("ix") == ix and cpu.sp == sp + 2
                    assert cpu.writes <= set(addresses) | set(range(sp - 256, sp))
print("960 record-address executions passed")
