#!/usr/bin/env python3
"""Compile the real WinHTTP direct fallback with controlled native API doubles."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='C compiler, e.g. cl or gcc; otherwise CC/cc')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    provider = root / '01-Extended DLLs/KxNet'
    source = (provider / 'winhttp_open.c').read_text(encoding='utf-8-sig')
    # Both architectures already exported this name, at different ordinals.
    for mapping in ('WinHttpOpen=Ext_WinHttpOpen,@747',
                    'WinHttpOpen=_Ext_WinHttpOpen@20,@735'):
        if '"/EXPORT:' + mapping + '"' not in source:
            raise AssertionError('Keep the original architecture-specific WinHttpOpen ordinal')
    forwards = (provider / 'forwards.c').read_text(encoding='utf-8-sig')
    if re.search(r'/EXPORT:WinHttpOpen=winhttp\.WinHttpOpen[" ,]', forwards):
        raise AssertionError('The old WinHttpOpen forwarder must not bypass the wrapper')
    # The file contains only these real helpers/function after its export setup.
    production = source[source.index('STATIC BOOL KxNetDirectProxyStringEmpty'):]
    template = (here / 'test_winhttp_open.c').read_text(encoding='utf-8')
    configured = args.compiler or os.environ.get('CC')
    compiler = ([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or shutil.which('gcc') or 'cc']
    is_msvc = Path(compiler[0]).stem.lower() == 'cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-winhttp-') as temporary:
        temporary = Path(temporary)
        test = temporary / 'test_winhttp_open.c'
        test.write_text(template.replace('/* PRODUCTION */', production), encoding='utf-8')
        binary = temporary / ('winhttp.exe' if os.name == 'nt' else 'winhttp')
        if is_msvc:
            command = compiler + ['/nologo', '/MT', '/W4', '/WX', '/TC', '/Od', str(test), '/Fe' + str(binary)]
        else:
            command = compiler + ['-std=gnu89', '-O2', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement', str(test), '-o', str(binary)]
        subprocess.run(command, cwd=temporary, check=True)
        subprocess.run([str(binary)], cwd=temporary, check=True, timeout=20)
    print('WinHTTP production-function regressions passed.', flush=True)


if __name__ == '__main__':
    main()
