#!/usr/bin/env python3
"""Exercise actual UAC admission/protocol and client code using API doubles."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='C compiler; otherwise CC/cc')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    type_source = (root / '00-Common Headers/KexDll.h').read_text(encoding='utf-8-sig')
    types = '\n'.join(re.search(r'typedef ' + kind + r' _' + name + r'\s*\{.*?\}\s*TYPEDEF_TYPE_NAME\(' + name + r'\);', type_source, re.S).group().replace('TYPEDEF_TYPE_NAME(' + name + ')', name) for kind, name in [('enum', 'KEX_WIN_VER_SPOOF'), ('struct', 'KEX_IFEO_PARAMETERS')])
    types += '\n' + '\n'.join(re.findall(r'^#define KEX_STRONGSPOOF_[^\n]*', type_source, re.M))
    spec = importlib.util.spec_from_file_location('extract', here.parent / 'chrome/run-tests.py')
    extract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extract)
    protocol = (root / '00-Common Headers/KexUac.h').read_text(encoding='utf-8-sig')
    protocol = re.sub(r'^#(?:include|pragma once).*$', '', protocol, flags=re.M)
    helper = (root / 'KexElevate/helper.c').read_text(encoding='utf-8-sig')
    pure = '\n'.join(extract.function(helper, 'static BOOLEAN ' + name + '(')
                     for name in ('KxuNonceValid', 'KxuParsePid', 'KxuParseCreationTime'))
    client = (root / '01-Extended DLLs/KxUac/client.c').read_text(encoding='utf-8-sig')
    actual = '\n'.join(extract.function(client, prefix) for prefix in (
        'static BOOL KxuExecute(', 'BOOL WINAPI KxuShellExecuteExW(',
        'static PWSTR KxuUnicode(', 'static BOOL KxuExecuteA(',
        'BOOL WINAPI KxuShellExecuteExA(', 'HINSTANCE WINAPI KxuShellExecuteW(',
        'HINSTANCE WINAPI KxuShellExecuteA('))
    helper_main = extract.function(helper, 'DWORD WINAPI KxuElevateMain(')
    route_source = (root / 'KexDll/uacroute.c').read_text(encoding='utf-8-sig')
    route = '\n'.join(extract.function(route_source, prefix) for prefix in (
        'static BOOLEAN KexpUacNameEqual(', 'static BOOLEAN KexpUacRange(',
        'static BOOLEAN KexpUacDataSlot(', 'KEXAPI PVOID NTAPI KexUacGetReplacement('))
    groups = [('protocol', pure), ('client', actual), ('helper', helper_main), ('route', route)]
    for name, functions in groups:
        template = (here / ('test_' + name + '.c')).read_text(encoding='utf-8')
        doubled = set(re.findall(r'^#define\s+(\w+)\s+Mock\w+', template, re.M))
        stripped = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', functions, flags=re.S)
        calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', stripped))
        allowed = {'if', 'for', 'return', 'sizeof', 'wcslen', 'ZeroMemory', 'FAILED', 'ARRAYSIZE', 'BOOL', 'HINSTANCE', 'NT_SUCCESS', 'max'}
        unknown = calls - doubled - allowed - {c for c in calls if c.startswith(('Kxu', 'KexpUac', 'KexUac'))}
        if unknown:
            raise AssertionError(name + ': add an explicit safe API double before running: ' + ', '.join(sorted(unknown)))
    configured = args.compiler or os.environ.get('CC')
    compiler = ([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or 'cc']
    msvc = Path(compiler[0]).stem.lower() == 'cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-uac-contracts-') as directory:
        temporary = Path(directory)
        shutil.copyfile(here / 'uac_test_shim.h', temporary / 'uac_test_shim.h')
        (temporary / 'production_types.h').write_text(types, encoding='utf-8')
        for name, additions in groups:
            source = temporary / ('test_' + name + '.c')
            text = (here / source.name).read_text(encoding='utf-8')
            source.write_text(text.replace('/* ACTUAL_PROTOCOL */', protocol).replace('/* ACTUAL_PARSERS */', pure).replace('/* ACTUAL_FUNCTIONS */', additions), encoding='utf-8')
            binary = temporary / (name + ('.exe' if os.name == 'nt' else ''))
            flags = ['/nologo', '/W4', '/WX', '/TC', '/Od'] if msvc else ['-std=gnu89', '-O2', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement']
            output = ['/Fe' + str(binary)] if msvc else ['-o', str(binary)]
            subprocess.run(compiler + flags + [str(source)] + output, cwd=temporary, check=True)
            subprocess.run([str(binary)], cwd=temporary, check=True, timeout=20)
    print('UAC production-function contracts passed.', flush=True)


if __name__ == '__main__':
    main()
