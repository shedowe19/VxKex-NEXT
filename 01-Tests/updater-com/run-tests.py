#!/usr/bin/env python3
"""Run the actual configured-updater COM functions with API doubles only."""

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
    parser.add_argument('--compiler', help='C compiler, e.g. cl or cc; otherwise CC/cc')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    # Reuse the established tokenizer, which ignores braces inside strings/comments.
    spec = importlib.util.spec_from_file_location('chrome_test_functions', here.parent / 'chrome/run-tests.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    source = root / '01-Extended DLLs/KxCom'
    header = (source / 'updaterfallback.h').read_text(encoding='utf-8-sig')
    functions = header[header.index('STATIC volatile LONG KxpUpdaterFallbackBusy'):]
    wrapper = module.function((source / 'firefoxpatch.c').read_text(encoding='utf-8-sig'),
                              'KXCOMAPI HRESULT WINAPI Ext_CoCreateInstance(')
    # Reject a newly introduced OS call until the test explicitly doubles it.
    # In particular, SDK declarations must not silently enable a real launcher.
    template = (here / 'test_updater_com.c').read_text(encoding='utf-8')
    doubled = set(re.findall(r'^#define\s+(\w+)\s+Mock\w+', template, re.M))
    helpers = set(re.findall(r'\b(Kxp\w+)\s*\(', functions))
    pure = {'if', 'while', 'return', 'sizeof', 'SUCCEEDED', 'FAILED', 'NT_SUCCESS',
            'ARRAYSIZE', 'HRESULT_FROM_WIN32', 'IsEqualGUID', 'RtlZeroMemory',
            'StringEqual', 'StringEqualI', 'StringBeginsWith', 'wcslen', 'wcsrchr',
            'Ext_CoCreateInstance'}
    stripped = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                      '', functions + '\n' + wrapper, flags=re.S)
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', stripped))
    unknown = calls - doubled - helpers - pure
    if unknown:
        raise AssertionError('Add explicit safe API doubles before running new calls: ' + ', '.join(sorted(unknown)))
    configured = args.compiler or os.environ.get('CC')
    compiler = ([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or 'cc']
    is_msvc = Path(compiler[0]).stem.lower() == 'cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-updater-com-') as temporary:
        temporary = Path(temporary)
        shutil.copyfile(here / 'test_platform.h', temporary / 'test_platform.h')
        test = temporary / 'test_updater_com.c'
        test.write_text(template.replace('/* PRODUCTION_FUNCTIONS */', functions + '\n' + wrapper), encoding='utf-8')
        binary = temporary / ('com_activation_contract.exe' if os.name == 'nt' else 'com_activation_contract')
        if is_msvc:
            command = compiler + ['/nologo', '/W4', '/WX', '/TC', '/Od', str(test), '/Fe' + str(binary)]
        else:
            command = compiler + ['-std=gnu89', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-Wdeclaration-after-statement', str(test), '-o', str(binary)]
        subprocess.run(command, cwd=temporary, check=True)
        subprocess.run([str(binary)], cwd=temporary, check=True, timeout=20)
    print('Configured updater COM production-function regressions passed.', flush=True)


if __name__ == '__main__':
    main()
