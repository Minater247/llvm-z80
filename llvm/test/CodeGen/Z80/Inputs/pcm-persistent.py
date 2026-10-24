"""Compile and execute ordinary-C persistent state and real call chains."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('state', here / 'pcm-state-check.py')
state = importlib.util.module_from_spec(spec)
spec.loader.exec_module(state)
llc, output = sys.argv[1:]
output = Path(output)
output.mkdir(exist_ok=True)
for allocator in ('greedy', 'z80'):
    for bank in (False, True):
        name = allocator + ('_bank' if bank else '_default')
        def compile(file, function, stream=False):
            path = output / (name + '-' + file.stem + ('-stream' if stream else '') + '.s')
            subprocess.run([llc, '-verify-machineinstrs', '-regalloc='+allocator,
                            '-mattr=' + ('+alternate-regs' if bank else '-alternate-regs') +
                            (',+expnoints' if stream else ''), str(file), '-o', str(path)], check=True)
            return state.function(path.read_text(), function)
        runner = compile(here/'pcm-run.ll', 'pcm_run')
        inline = compile(here/'pcm-state.ll', 'pcm_advance')
        advance = compile(here/'pcm-advance.ll', 'pcm_advance')
        leaf = compile(here.parent/'pcm-mix.ll', 'pcm_mix2')
        reports = {'state': state.check(runner, {name: inline}),
                   'calls': state.check(runner, {name: advance+'\n'+leaf}, calls_per_block=2)}
        # Protect grouping around calls from regressing into per-field reloads.
        if allocator == 'z80':
            assert reports['calls']['comparison_by_completed_blocks'][name]['2']['max_t_states'] <= 2020
            # Cursor reuse must keep the direct worker spill-free. Include all
            # worker entry/exit traffic in the execution ceiling.
            assert reports['state']['comparison_by_completed_blocks'][name]['2']['max_t_states'] <= 1242
            assert '(ix -' not in inline and 'ld\tsp,' not in inline
        if not bank:
            stream = compile(here.parent/'pcm-stream.ll', 'pcm_mix2', stream=True)
            reports['stream_calls'] = state.check(runner, {name: advance+'\n'+stream}, calls_per_block=2)
            if allocator == 'z80':
                assert reports['stream_calls']['comparison_by_completed_blocks'][name]['2']['max_t_states'] <= 2064
        print(name, json.dumps({k: r['comparison_by_completed_blocks'] for k,r in reports.items()}))
