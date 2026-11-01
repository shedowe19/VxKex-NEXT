#!/usr/bin/env python3
"""Compile and exercise the real propagation cleanup routine with NT doubles."""

import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def extract_function(text, signature):
    start = text.index(signature)
    first_brace = text.index('{', start)
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for token in tokens.finditer(text, first_brace):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                return text[start:token.end()]
    raise ValueError('Unterminated function: ' + signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='C compiler, e.g. cl or gcc; otherwise use CC/cc')
    parser.add_argument('--source', type=Path, help='Alternate propagte.c, for baseline regression reproduction')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    source = args.source or root / 'KexDll/propagte.c'
    production = extract_function(source.read_text(encoding='utf-8-sig'),
                                  'STATIC VOID KexpCleanupPropagationRemains(')
    configured = args.compiler or os.environ.get('CC')
    compiler = ([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or shutil.which('gcc') or 'cc']
    is_msvc = Path(compiler[0]).stem.lower() == 'cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-propagation-') as temporary:
        temporary = Path(temporary)
        shutil.copyfile(here / 'test_platform.h', temporary / 'test_platform.h')
        test = temporary / 'test_cleanup.c'
        text = (here / 'test_cleanup.c').read_text(encoding='utf-8')
        test.write_text(text.replace('/* PRODUCTION_FUNCTION */', production), encoding='utf-8')
        binary = temporary / ('test_cleanup.exe' if os.name == 'nt' else 'test_cleanup')
        if is_msvc:
            command = compiler + ['/nologo', '/W4', '/WX', '/TC', '/Od', str(test), '/Fe' + str(binary)]
        else:
            command = compiler + ['-std=gnu89', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-Wdeclaration-after-statement', str(test), '-o', str(binary)]
        subprocess.run(command, cwd=temporary, check=True)
        subprocess.run([str(binary)], cwd=temporary, check=True, timeout=20)
    print('Propagation cleanup production-function regressions passed.', flush=True)


if __name__ == '__main__':
    main()
