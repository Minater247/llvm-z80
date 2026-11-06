"""Check bank-resident addresses through polling, nested calls and wrapping."""
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


class DeviceCPU(x.CPU):
    def __init__(self, seed, port, delay):
        super().__init__(random.Random(seed))
        self.port, self.delay, self.pending = port, delay, delay
        self.trace = []

    def read(self, operand, width=1):
        if operand.startswith('(') and self.addr(operand) == self.port:
            assert width == 1
            if self.pending:
                self.pending -= 1
                return 0
            return 1
        return super().read(operand, width)

    def write(self, operand, value, width=1):
        if operand.startswith('(') and self.addr(operand) in (self.port, self.port + 1):
            assert width == 1
            self.trace.append((self.addr(operand), value & 255))
            self.pending = self.delay
        super().write(operand, value, width)


checks = 0
for allocator in ('z80', 'greedy'):
    for bank in (False, True):
        for resident, arithmetic in ((1, False), (2, False), (1, True), (2, True)):
            for port in (8960, 12320):
                ir = directory / f'{allocator}-{bank}-{resident}-{arithmetic}-{port}.ll'
                text = Path(source).read_text().replace('8960', str(port)).replace('8961', str(port + 1))
                if arithmetic:
                    text = text.replace('  store volatile i8 %control,',
                                        '  %wire = xor i8 %control, 3\n  store volatile i8 %wire,')
                ir.write_text(text)
                out = ir.with_suffix('.s')
                subprocess.run([llc, '-z80-cache-bank-addresses',
                                '-z80-bank-address-count='+str(resident),
                                '-regalloc='+allocator,
                                '-mattr='+('+' if bank else '-')+'alternate-regs',
                                '-verify-machineinstrs', str(ir), '-o', str(out)], check=True)
                assembly = out.read_text()
                if bank and allocator == 'z80' and not arithmetic:
                    leaf = assembly.split('emit.state:', 1)[1].split('.Lfunc_end', 1)[0]
                    assert len(re.findall(r'\bexx\b', leaf)) == 2, leaf
                    assert leaf.index('exx') < leaf.index('bit'), leaf
                    assert not re.search(r'\b(push|pop)\b', leaf), leaf
                assembly = re.sub(r'\bcursor\b', '8192', assembly)
                assembly = re.sub(r'\btimer\b', '8194', assembly)
                program = x.Program(assembly)
                for start in range(256):
                    for delay in (0, 1, 3):
                        for entry, count in (('run', 8), ('outside', 1)):
                            cpu = DeviceCPU(start, port, delay)
                            cpu.mem[8192:8195] = bytes((start, 49, 21))
                            expected = []
                            for i in range(count):
                                sample = cpu.mem[0x3100 + ((start + i) & 255)]
                                expected += [(port, 39), (port + 1, 22 if arithmetic else 21),
                                             (port, 42), (port + 1, sample)]
                            sp, ix = cpu.sp, cpu.get('ix')
                            cpu.run(program, entry=entry)
                            result = 0x3100 | ((start + count) & 255)
                            assert cpu.trace == expected, (allocator, bank, resident, arithmetic, start, delay, entry)
                            assert cpu.mem[8192] + 256 * cpu.mem[8193] == result
                            assert cpu.sp == sp + 2 and cpu.get('ix') == ix
                            if entry == 'outside':
                                assert cpu.get('hl') == result
                            checks += 1
print(f'{checks} bank-address executions passed')
