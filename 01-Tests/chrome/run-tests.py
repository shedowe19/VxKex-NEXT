#!/usr/bin/env python3
"""Compile and run the production Chrome compatibility functions with API doubles."""

import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def function(text, prefix):
    """Keep the real signature/body; ignore braces in comments and literals."""
    start = text.index(prefix)
    brace = text.index('{', start)
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for token in tokens.finditer(text, brace):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                return text[start:token.end()]
    raise ValueError('Unterminated production function: ' + prefix)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='C compiler, e.g. cl or gcc; otherwise use CC/cc')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    cryp = (root / '01-Extended DLLs/KxCryp/bcrypt.c').read_text(encoding='utf-8-sig')
    base = (root / '01-Extended DLLs/KxBase/stubs.c').read_text(encoding='utf-8-sig')
    public = (root / '00-Common Headers/KxCryp.h').read_text(encoding='utf-8-sig')
    prefix = 'KXCRYPAPI SECURITY_STATUS WINAPI NCryptCreateClaim('
    first = public.index(prefix)
    declaration = public[first:public.index(';', first) + 1]
    implementations = {
        'create_claim': function(cryp, prefix),
        'get_proc_address': function(base, 'KXBASEAPI FARPROC WINAPI Stub_GetProcAddress('),
    }
    exports = (root / '01-Extended DLLs/KxCryp/kxcryp.def').read_text(encoding='utf-8-sig')
    if not re.search(r'^\s*NCryptCreateClaim\s+@226\s*$', exports, re.M):
        raise AssertionError('NCryptCreateClaim must remain exported at ordinal226')
    forwards = (root / '01-Extended DLLs/KxCryp/forwards.c').read_text(encoding='utf-8-sig')
    if not re.search(r'^\s*#pragma\s+comment\s*\(\s*linker\s*,\s*"/EXPORT:AcceptSecurityContext=secur32\.AcceptSecurityContext,@1"\s*\)', forwards, re.M):
        raise AssertionError('Keep the original ordinal1 anchor; @226 alone shifts existing MSVC auto ordinals')
    exports = (root / '01-Extended DLLs/KxBase/kxbase.def').read_text(encoding='utf-8-sig')
    if not re.search(r'^\s*GetProcAddress\s*=\s*Stub_GetProcAddress\s*$', exports, re.M):
        raise AssertionError('The public GetProcAddress export must use the tested wrapper')
    configured = args.compiler or os.environ.get('CC')
    compiler = ([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or shutil.which('gcc') or 'cc']
    is_msvc = Path(compiler[0]).stem.lower() == 'cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-chrome-') as temporary:
        temporary = Path(temporary)
        shutil.copyfile(here / 'test_platform.h', temporary / 'test_platform.h')
        for name, production in implementations.items():
            test = temporary / ('test_' + name + '.c')
            template = (here / ('test_' + name + '.c')).read_text(encoding='utf-8')
            test.write_text(template.replace('/* PRODUCTION_DECLARATION */', declaration).replace('/* PRODUCTION_FUNCTION */', production), encoding='utf-8')
            binary = temporary / (name + ('.exe' if os.name == 'nt' else ''))
            if is_msvc:
                command = compiler + ['/nologo', '/W4', '/WX', '/TC', '/Od', str(test), '/Fe' + str(binary)]
            else:
                command = compiler + ['-std=gnu89', '-O2', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement', str(test), '-o', str(binary)]
            subprocess.run(command, cwd=temporary, check=True)
            subprocess.run([str(binary)], cwd=temporary, check=True, timeout=20)
    print('Chrome compatibility production-function regressions passed.', flush=True)


if __name__ == '__main__':
    main()
