#!/usr/bin/env python3
"""Windows VC10 contract suite; never installs DLLs or runs GPU/driver probes."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SUITES = {
    'process': 'Production source with controlled NT APIs and real pointer protection',
    'cfgmgr': 'Production source, real message window/registration, synthetic owned events',
    'async': 'Production source, native COM/FTM/GIT and deterministic GIT call fencing',
    'winrt': 'Full production HSTRING/factory/URI/vector bodies with native COM/URLMON/FTM references',
    'nt': 'Expanded owned native reference versus production-body class58 fallback',
    'graphics': 'Production outer-COM and D3D11 bridge sources, independent lifetime/ABI backends; no GPU rendering',
    'cpuset': 'Production CPU set topology and affinity implementation with native reference',
}
NATIVE_FLAGS = ['/nologo', '/W4', '/WX', '/O2', '/MT', '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0601', '/DWINVER=0x0601']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')


class Commands:
    def __init__(self, directory, environment):
        self.directory = directory
        self.environment = environment
        self.rows = []

    def run(self, label, argv, timeout=120):
        start = time.monotonic()
        argv = list(map(str, argv))
        row = {'label': label, 'argv': argv, 'timeoutSeconds': timeout}
        try:
            result = subprocess.run(argv, cwd=self.directory, env=self.environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
            stdout, stderr = result.stdout, result.stderr
            row['exitCode'] = result.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or b'', error.stderr or b''
            row.update(exitCode=None, timedOut=True)
        row['elapsedSeconds'] = round(time.monotonic() - start, 3)
        (self.directory / (label + '.stdout')).write_bytes(stdout)
        (self.directory / (label + '.stderr')).write_bytes(stderr)
        self.rows.append(row)
        write_json(self.directory / 'commands.json', self.rows)
        if row['exitCode'] != 0:
            raise RuntimeError('{} failed ({}); see {}'.format(label, row['exitCode'], self.directory))
        return stdout.decode('utf-8', errors='replace')


def toolchain(sdk, arch, temporary):
    vc = sdk / 'Program Files (x86)/Microsoft Visual Studio 10.0/VC'
    ws = sdk / 'Program Files/Microsoft SDKs/Windows/v7.1'
    compiler = vc / ('bin/x86_amd64' if arch == 'x64' else 'bin') / 'cl.exe'
    link = compiler.parent / 'link.exe'
    for file in [compiler, link, ws / 'Include/windows.h', vc / 'include/stdio.h']:
        if not file.is_file():
            raise RuntimeError('Incomplete extracted Win7SDK: missing ' + str(file))
    env = os.environ.copy()
    env['CL'] = env['_CL_'] = ''
    env['TEMP'] = env['TMP'] = str(temporary)
    path = [sdk / 'vcredist/x86', sdk / 'vcredist/x64', compiler.parent,
            vc / 'bin', vc.parent / 'Common7/IDE', ws / 'Bin', ws / 'Bin/x64']
    env['PATH'] = ';'.join(map(str, path)) + ';' + env.get('PATH', '')
    env['INCLUDE'] = ';'.join(map(str, [REPO / '00-Common Headers', ws / 'Include', vc / 'include']))
    env['LIB'] = ';'.join(map(str, [REPO / '00-Import Libraries',
                                  ws / ('Lib/x64' if arch == 'x64' else 'Lib'),
                                  vc / ('lib/amd64' if arch == 'x64' else 'lib')]))
    return compiler, link, env


def machine(path, arch):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    expected = 0x8664 if arch == 'x64' else 0x14c
    if data[:2] != b'MZ' or data[offset:offset + 4] != b'PE\0\0' or struct.unpack_from('<H', data, offset + 4)[0] != expected:
        raise RuntimeError('Wrong executable architecture: ' + str(path))


def smoke(commands, compiler, arch):
    path = commands.directory / 'architecture.c'
    path.write_text('#include <stdio.h>\ntypedef char ExpectedPointerSize[(sizeof(void*)==%u)?1:-1];\nint main(void){puts("toolchain-ok");return 0;}\n' % (8 if arch == 'x64' else 4))
    executable = commands.directory / 'architecture.exe'
    commands.run('compile', [compiler] + NATIVE_FLAGS + [path, '/Fo' + str(commands.directory / 'architecture.obj'), '/Fe' + str(executable)])
    machine(executable, arch)
    if 'toolchain-ok' not in commands.run('execute', [executable]):
        raise RuntimeError('Toolchain smoke executable did not run')


def existing_runner(commands, compiler, name, arch):
    directory = {'process': 'process-modern', 'cfgmgr': 'cfgmgr-modern', 'async': 'async-review', 'winrt': 'winrt-modern', 'cpuset': 'cpuset-modern'}[name]
    arguments = [sys.executable, REPO / '01-Tests' / directory / 'run-tests.py',
                 '--compiler', compiler, '--keep-output', commands.directory]
    if name == 'cpuset':
        arguments.extend(['--ntdll-lib', 'ntdll_x64.lib' if arch == 'x64' else 'ntdll_x86.lib'])
    output = commands.run('runner', arguments, timeout=150)
    patterns = {'process': r'process-memory contract: ([1-9][0-9]*) assertions, 0 failures',
                'cfgmgr': r'cfgmgr notification contract: ([1-9][0-9]*) assertions, 0 failures',
                'async': r'OVERLAP_SUMMARY\|checks=([1-9][0-9]*)\|failures=0\|live_allocations=0\|live_handlers=0\|live_git=0\|overlaps=0',
                'winrt': r'WINRT_SUMMARY\|checks=([1-9][0-9]*)\|failures=0\|owned_allocations=0\|instance_marshalers=0\|cached_marshalers=[0-9]+',
                'cpuset': r'CPUSET_SUMMARY\|checks=([1-9][0-9]*)\|failures=0'}
    match = re.search(patterns[name], output)
    if not match:
        raise RuntimeError('Missing successful runtime summary for ' + name)
    if name == 'winrt' and ('NATIVE_STRING_COMPARE|pairs=25|inputs_separate=1' not in output or
                            'NATIVE_URI_REFERENCE|opaque_native_inputs=1|nul_truncation=1' not in output or
                            output.count('creator_reference_released=1') != 8):
        raise RuntimeError('Incomplete WinRT native-reference/marshaling controls')
    return {'assertions': int(match.group(1)), 'evidence': SUITES[name]}


def native_handle_control(text):
    required = ['native_owner=1|kex_absent=1', 'OWNED|self_events=3|self_jobs=2|child_events=3|child_jobs=2|child_live=1',
                'CHILD_EXIT|code=259', 'COMPLETE|inventory_only=1|no_privilege_or_system_changes=1']
    if any(part not in text for part in required):
        raise RuntimeError('Missing owned native handle-table controls')
    for label in ['self-pseudo', 'child-full', 'child-query-dup']:
        lines = [s for s in text.splitlines() if s.startswith('QUERY|' + label + '|length=4096|special=0|')]
        if len(lines) != 1 or '|status=0x00000000|' not in lines[0] or '|known=0x1f|' not in lines[0]:
            raise RuntimeError('Modern native class58 oracle unavailable/incorrect for ' + label)


def nt_contract(commands, compiler, link, arch):
    gate = commands.directory / 'HandleTablesGate.c'
    commands.run('generate', [sys.executable, REPO / '01-Tests/nt-gate-review/make-fixture.py', '--out', gate])
    native = commands.directory / 'HandleTablesNative.exe'
    commands.run('native-compile', [compiler] + NATIVE_FLAGS + [gate, '/Fo' + str(commands.directory / 'native.obj'),
                 '/Fe' + str(native), '/link', '/SUBSYSTEM:CONSOLE,6.01', '/MANIFEST:NO', 'kernel32.lib'])
    native_log = commands.run('native', [native])
    native_handle_control(native_log)
    source = REPO / 'KexDll/ntps.c'
    commands.run('actual-headers-compile', [compiler, '/nologo', '/c', '/W3', '/WX', '/O2', '/Zl', '/Gz',
                 '/DWIN32', '/DNDEBUG', '/D_WINDOWS', '/D_USRDLL', '/DKEXDLL_EXPORTS', source,
                 '/Fo' + str(commands.directory / 'actual-headers.obj')])
    lines = source.read_text(encoding='utf-8-sig').splitlines(keepends=True)
    if lines[0].strip() != '#include "buildcfg.h"' or lines[1].strip() != '#include "kexdllp.h"':
        raise RuntimeError('NT production include prefix changed; review extraction')
    body = commands.directory / 'nt-body.c'
    body.write_text('#include "nt-query-shim.h"\n' + ''.join(lines[2:]), encoding='utf-8')
    objects = []
    for path, label in [(body, 'body'), (HERE / 'nt-query-availability.c', 'availability'), (gate, 'fixture')]:
        obj = commands.directory / (label + '.obj')
        commands.run(label + '-compile', [compiler] + NATIVE_FLAGS + ['/c', '/I' + str(HERE),
                     '/DHANDLE_TABLE_FALLBACK_TEST', path, '/Fo' + str(obj)])
        objects.append(obj)
    fallback = commands.directory / 'HandleTablesFallback.exe'
    commands.run('fallback-link', [link, '/nologo', '/OUT:' + str(fallback), '/SUBSYSTEM:CONSOLE,6.01', '/MANIFEST:NO',
                 '/DYNAMICBASE', '/NXCOMPAT'] + objects + ['kernel32.lib', 'ntdll_' + ('x64' if arch == 'x64' else 'x86') + '.lib'])
    commands.run('fallback', [fallback])
    report = commands.directory / 'comparison.json'
    commands.run('compare', [sys.executable, REPO / '01-Tests/nt-modern/verify-handle-tables.py',
                 commands.directory / 'native.stdout', commands.directory / 'fallback.stdout', '--out', report])
    result = json.loads(report.read_text())
    if result['status'] != 'PASS' or result['comparisons'] < 300 or result['assertions'] < 1000:
        raise RuntimeError('Incomplete expanded NT comparison')
    return {'comparisons': result['comparisons'], 'assertions': result['assertions'], 'evidence': SUITES['nt'],
            'limit': 'Native modern-host oracle and forced availability seam; installed Win7 wrapper requires separate tests'}


def graphics_contract(commands, compiler):
    excludes = ['seh32', 'KxCfgHlp', 'KxMi', 'KxBase', 'KexGui', 'KexPathCch', 'KexSmp', 'KexMls', 'KexW32ML']
    components = []
    cases = [('GraphicsContractTests', r'PASS\|assertions=([1-9][0-9]*)\|failures=0\|live_wrapper_tables=0'),
             ('DxgiProxyContracts', r'PASS\|dxgi_proxy_contracts\|checks=([1-9][0-9]*)\|failures=0\|live=0'),
             ('D3D11BridgeContracts', r'PASS\|d3d11_bridge_contracts\|checks=(1399)\|failures=0\|calls=36\|live=0')]
    for label, pattern in cases:
        executable = commands.directory / (label + '.exe')
        commands.run(label + '-compile', [compiler, '/nologo', '/W3', '/WX', '/O2', '/MT', '/Gy', '/DUNICODE', '/D_UNICODE',
                     '/DNDEBUG', '/DNO_KEXDLL_LIB', '/DKW32MLDECLSPEC=', '/DKSMPDECLSPEC=', '/DKMLSDECLSPEC=',
                     REPO / ('01-Tests/graphics-modern/' + label + '.c'),
                     '/Fo' + str(commands.directory / (label + '.obj')), '/Fe' + str(executable), '/link',
                     '/ENTRY:mainCRTStartup', '/SUBSYSTEM:CONSOLE,6.01', '/MANIFEST:NO', '/OPT:REF', '/DYNAMICBASE', '/NXCOMPAT'] +
                     ['/NODEFAULTLIB:' + name + '.lib' for name in excludes] + ['libcmt.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'uuid.lib'])
        output = commands.run(label + '-mock-contract', [executable], timeout=45)
        match = re.search(pattern, output)
        if not match:
            raise RuntimeError('Missing graphics runtime success: ' + label)
        if label == 'GraphicsContractTests' and not re.search(r'PASS\|d2d_outer_proxy\|checks=([1-9][0-9]*)\|failures=0\|live=0', output):
            raise RuntimeError('Missing mandatory independent D2D outer-proxy checks')
        component = {'suite': label, 'assertions': int(match.group(1))}
        if label == 'D3D11BridgeContracts':
            component.update(mockNativeCalls=36, actualDriverExecuted=False,
                             seam='Production thunks; source-included controlled D3D11 native function callbacks')
        components.append(component)
    return {'assertions': sum(row['assertions'] for row in components), 'components': components,
            'evidence': SUITES['graphics'], 'hardwareExecuted': False,
            'limit': 'Native-object private tails, identity/lifetime and controlled D3D11 thunk boundaries; real driver interop, outputs and swapchain parent graphs remain separate'}


def build_probes(commands, compiler):
    files = [('GraphicsModernProbe', REPO / '01-Tests/graphics-modern/GraphicsModernProbe.cpp',
              ['/EHsc'], ['kernel32.lib', 'user32.lib', 'gdi32.lib', 'd3d11.lib', 'dxgi.lib']),
             ('StrictHandleNative', REPO / '01-Tests/nt-modern/StrictHandleNative.c', [], ['kernel32.lib'])]
    for name, source, extra, libs in files:
        commands.run(name + '-compile', [compiler] + NATIVE_FLAGS + extra + [source,
                     '/Fo' + str(commands.directory / (name + '.obj')), '/Fe' + str(commands.directory / (name + '.exe')),
                     '/link', '/SUBSYSTEM:CONSOLE,6.01', '/MANIFEST:NO', '/DYNAMICBASE', '/NXCOMPAT'] + libs)
    return {'evidence': 'Build only: physical graphics and own-process strict-handle inventory probes', 'executed': False}


def source_pins():
    files = set(HERE.glob('*'))
    for name in ['process-modern', 'cfgmgr-modern', 'async-review', 'winrt-modern', 'nt-modern', 'nt-gate-review', 'graphics-modern', 'cpuset-modern']:
        files.update((REPO / '01-Tests' / name).glob('*'))
    files.update((REPO / '00-Common Headers').glob('*.h'))
    files.update((REPO / '00-Import Libraries').glob('*.lib'))
    for name in ['KxBase', 'KxCom', 'KxDx']:
        files.update((REPO / '01-Extended DLLs' / name).glob('*.h'))
    files.update([REPO / 'KexDll/ntps.c', REPO / 'KexDll/buildcfg.h', REPO / 'KexDll/kexdllp.h',
                  REPO / '01-Extended DLLs/KxBase/process.c', REPO / '01-Extended DLLs/KxBase/cfgmgr.c',
                  REPO / '01-Extended DLLs/KxBase/cpuset.c', REPO / '01-Extended DLLs/KxBase/thread.c',
                  REPO / '01-Extended DLLs/KxCom/asyncbool.c'])
    files.update((REPO / '01-Extended DLLs/KxDx').glob('*.c'))
    files.update((REPO / '01-Extended DLLs/KxCom').glob('*.c'))
    return {str(p.relative_to(REPO)): digest(p) for p in sorted(files) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, help='Extracted Win7SDK bundle root; no installation/registry import')
    parser.add_argument('--architectures', nargs='+', choices=['x86', 'x64'], default=['x86', 'x64'])
    parser.add_argument('--suites', nargs='+', choices=list(SUITES), default=list(SUITES))
    parser.add_argument('--out', type=Path, help='New output directory for commands, raw logs and summary')
    parser.add_argument('--build-probes', action='store_true', help='Compile graphics/strict-handle probes; never execute them')
    parser.add_argument('--list', action='store_true', help='Show suite boundaries without running anything')
    args = parser.parse_args()
    if args.list:
        print(json.dumps(SUITES, indent=2)); return 0
    if os.name != 'nt': parser.error('Windows-only runner; portable suites use 01-Tests/run-tests.py')
    if not args.sdk_root or not args.out: parser.error('--sdk-root and --out are required')
    output = args.out.resolve(); output.mkdir(parents=True, exist_ok=False)
    pins = source_pins()
    report = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'host': platform.platform(), 'python': platform.python_version(),
              'suiteClass': 'Windows contracts, not hardware/installed-Kex acceptance', 'sourceSha256': pins,
              'toolchains': {}, 'results': []}
    def save(): write_json(output / 'summary.json', report)
    save()
    for arch in dict.fromkeys(args.architectures):
        arch_out = output / arch; arch_out.mkdir()
        try:
            compiler, link, environment = toolchain(args.sdk_root.resolve(), arch, arch_out)
            report['toolchains'][arch] = {'compiler': str(compiler), 'compilerSha256': digest(compiler), 'linkerSha256': digest(link)}
            smoke_out = arch_out / 'toolchain'; smoke_out.mkdir()
            smoke(Commands(smoke_out, environment), compiler, arch)
        except Exception as error:
            report['results'].append({'arch': arch, 'suite': 'toolchain', 'status': 'FAIL', 'error': str(error)})
            save(); continue
        for name in dict.fromkeys(list(args.suites) + (['build-probes'] if args.build_probes else [])):
            directory = arch_out / name; directory.mkdir()
            commands = Commands(directory, environment)
            row = {'arch': arch, 'suite': name}
            try:
                if name in ['process', 'cfgmgr', 'async', 'winrt', 'cpuset']: detail = existing_runner(commands, compiler, name, arch)
                elif name == 'nt': detail = nt_contract(commands, compiler, link, arch)
                elif name == 'graphics': detail = graphics_contract(commands, compiler)
                else: detail = build_probes(commands, compiler)
                detail['executables'] = []
                for exe in directory.rglob('*.exe'):
                    machine(exe, arch)
                    detail['executables'].append({'path': str(exe.relative_to(output)), 'sha256': digest(exe), 'bytes': exe.stat().st_size})
                row.update(status='PASS', **detail)
            except Exception as error:
                row.update(status='FAIL', error=str(error))
            report['results'].append(row); save()
            print('{} {}: {}'.format(arch, name, row['status']), flush=True)
    report['inputsUnchanged'] = source_pins() == pins
    report['status'] = 'PASS' if report['inputsUnchanged'] and report['results'] and all(r['status'] == 'PASS' for r in report['results']) else 'FAIL'
    save()
    print('Windows modern API suite: ' + report['status'], flush=True)
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
