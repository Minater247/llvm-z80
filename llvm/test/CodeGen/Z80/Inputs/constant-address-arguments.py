"""Check preserved address arguments, byte-wrapping state and ordered I/O."""
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
    for bank, enabled in ((False, True), (True, False), (True, True)):
        for port in (8960, 12320):
            ir = directory / f'{allocator}-{bank}-{enabled}-{port}.ll'
            text = Path(source).read_text().replace('8960', str(port)).replace('8961', str(port + 1))
            ir.write_text(text)
            flags = ['-regalloc='+allocator, '-mattr='+('+' if bank else '-')+'alternate-regs',
                     '-z80-pass-constant-addresses='+str(enabled).lower(), '-verify-machineinstrs']
            out = ir.with_suffix('.s')
            subprocess.run([llc, *flags, str(ir), '-o', str(out)], check=True)
            prepared = subprocess.check_output(
                [llc, *flags, '-stop-before=irtranslator', str(ir), '-o', '-'], text=True)
            assert ('"z80-address-arguments"' in prepared) == (bank and enabled)
            assembly = out.read_text()
            if bank and enabled and allocator == 'z80':
                for name in ('device', 'poll'):
                    body = assembly.split('\n'+name+':', 1)[1].split('.Lfunc_end', 1)[0]
                    assert len(re.findall(r'\bexx\b', body)) == 2, body
                    assert not re.search(r'\b(push|pop)\b', body), body
            program = x.Program(assembly)
            for seed in range(4):
                for start in (0, 254, 255):
                    for count in (0, 1, 2, 5):
                        for delay in (0, 2):
                            cpu = DeviceCPU(seed, port, delay)
                            cpu.mem[0xc000:0xc003] = bytes((start, 0xa0, 21))
                            cpu.put('de', 0x9000)
                            cpu.put('c', count)
                            expected = []
                            for i in range(count * 6):
                                sample = cpu.mem[0xa000 | ((start+i) & 255)]
                                expected += [(port, 39), (port+1, 21), (port, 42), (port+1, sample)]
                            sp, ix = cpu.sp, cpu.get('ix')
                            cpu.run(program, entry='run')
                            assert cpu.trace == expected, (allocator, bank, enabled, port, seed, start, count, delay)
                            assert cpu.mem[0xc000:0xc002] == bytes(((start+6*count) & 255, 0xa0))
                            assert cpu.mem[0x9000:0x9000+count] == bytes(range(count, 0, -1))
                            assert cpu.sp == sp+2 and cpu.get('ix') == ix
                            checks += 1
print(f'{checks} constant-address argument executions passed')

# An escaping leaf or an unoptimized caller retains the original interface.
for variant in ('escaped', 'optnone'):
    text = Path(source).read_text()
    if variant == 'escaped':
        text += '\n@escaped_device = global ptr @device\n'
    else:
        text = text.replace('minsize optsize nounwind', 'optnone noinline nounwind')
    ir = directory / (variant+'.ll')
    ir.write_text(text)
    prepared = subprocess.check_output(
        [llc, '-mattr=+alternate-regs', '-verify-machineinstrs',
         '-stop-before=irtranslator', str(ir), '-o', '-'], text=True)
    assert '"z80-address-arguments"' not in prepared, variant
