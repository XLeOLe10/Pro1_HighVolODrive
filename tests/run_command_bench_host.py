"""Run CPU-only tests of the real command port, bench sequencer and safety code."""
from pathlib import Path
import argparse
import os
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', default=r'D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe')
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
out = repo / 'build' / 'command-bench-host'
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] = str(Path(args.compiler).parent) + os.pathsep + env.get('PATH', '')
base = [args.compiler, '-std=c99', '-Wall', '-Wextra', '-Werror']
bench = ['-DAXIS_COMMAND_BENCH_ENABLE=1', '-DBOARD_POWER_STAGE_MODE=0']
log = ['CPU-only bench verification; target IRQ timing / physical outputs NOT tested.\n']

def run(command):
    result = subprocess.run(command, cwd=repo, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace')
    log.extend(['\nCOMMAND: '+subprocess.list2cmdline(command)+'\n', result.stdout,
                'EXIT: '+str(result.returncode)+'\n'])
    return result

def suite(name, flags, sources, expected):
    executable = out / (name + '.exe')
    built = run(base + flags + sources + ['-o', executable.as_posix()])
    if built.returncode != 0:
        raise RuntimeError(name+' compile failed:\n'+built.stdout)
    result = run([str(executable)])
    if result.returncode != 0 or expected not in result.stdout:
        raise RuntimeError(name+' test failed:\n'+result.stdout)
    print(next(line for line in result.stdout.splitlines() if expected in line))

try:
    version = run([args.compiler, '--version'])
    if version.returncode != 0: raise RuntimeError(version.stdout)
    suite('bench_sequence', ['-pedantic', '-I', 'Core/Inc'] + bench,
          ['tests/axis_command_bench_host.c','Core/Src/axis_command_bench.c',
           'Core/Src/axis_command_core.c'], '139 assertions')
    for name, flags in [('port_normal', []), ('port_bench', bench)]:
        suite(name, ['-pedantic', '-I', 'tests/port_stubs', '-I', 'Core/Inc'] + flags,
              ['tests/axis_command_port_host.c','Core/Src/axis_command.c',
               'Core/Src/axis_command_core.c'], 'real command port assertions')
    # GCC 4.5.3 warns about standard {0} GPIO initialization. Suppress only that
    # warning for the actual safety source and its stub, not all warnings.
    safety_flags = ['-Wno-missing-field-initializers', '-I', 'tests/host_safety',
                    '-I', 'Core/Inc']
    suite('safety_profile', safety_flags + bench,
          ['tests/safety_profile_host.c','Core/Src/board_safety.c'],
          '102 safety profile assertions')
    rows = [
        ('default_normal', [], 0, 1, True, ''),
        ('explicit_normal', ['AXIS_COMMAND_BENCH_ENABLE=0','BOARD_POWER_STAGE_MODE=1'],0,1,True,''),
        ('normal_disabled', ['AXIS_COMMAND_BENCH_ENABLE=0','BOARD_POWER_STAGE_MODE=0'],0,0,True,''),
        ('bench_disabled', ['AXIS_COMMAND_BENCH_ENABLE=1','BOARD_POWER_STAGE_MODE=0'],1,0,True,''),
        ('bench_missing_power', ['AXIS_COMMAND_BENCH_ENABLE=1'],1,1,False,'Command bench requires'),
        ('bench_gpio', ['AXIS_COMMAND_BENCH_ENABLE=1','BOARD_POWER_STAGE_MODE=1'],1,1,False,'Command bench requires'),
        ('bench_always_on', ['AXIS_COMMAND_BENCH_ENABLE=1','BOARD_POWER_STAGE_MODE=2'],1,2,False,'Command bench requires'),
        ('invalid_bench_value', ['AXIS_COMMAND_BENCH_ENABLE=2','BOARD_POWER_STAGE_MODE=0'],2,0,False,'must be 0 or 1'),
        ('normal_always_on', ['AXIS_COMMAND_BENCH_ENABLE=0','BOARD_POWER_STAGE_MODE=2'],0,2,False,'ALWAYS_ON needs'),
    ]
    for name, defines, bench_value, power, success, error in rows:
        flags = ['-D'+x for x in defines]
        probe = run(base + safety_flags + flags +
                    ['-DEXPECT_BENCH='+str(bench_value), '-DEXPECT_POWER='+str(power),
                     '-c', 'tests/safety_config_probe.c', '-o', (out/(name+'_probe.o')).as_posix()])
        actual = probe if probe.returncode else run(base + safety_flags + flags +
                    ['-c', 'Core/Src/board_safety.c', '-o', (out/(name+'_actual.o')).as_posix()])
        if success and actual.returncode:
            raise RuntimeError(name+' unexpectedly rejected:\n'+actual.stdout)
        if not success and (actual.returncode == 0 or error not in actual.stdout):
            raise RuntimeError(name+' not rejected by intended guard:\n'+actual.stdout)
        print('PROFILE PASS: '+name)
    print('PASS: bench/port/safety suites + 9 profile cases; no hardware operation')
    log.append('\nPASS: bench/port/safety suites + 9 profile cases\n')
finally:
    (out/'host-results.txt').write_text(''.join(log), encoding='utf-8', newline='\n')
