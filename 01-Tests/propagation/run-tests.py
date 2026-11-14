#!/usr/bin/env python3
"""Exercise the real propagation cleanup with adjacent-stub and failure canaries."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def function(text):
    start = text.index('STATIC VOID KexpCleanupPropagationRemains(')
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
    raise ValueError('Unterminated propagation cleanup')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='C compiler, e.g. cl or gcc')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    text = (root / 'KexDll/propagte.c').read_text(encoding='utf-8-sig')
    configured = args.compiler or os.environ.get('CC')
    compiler = ([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or shutil.which('gcc') or 'cc']
    is_msvc = Path(compiler[0]).stem.lower() == 'cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-propagation-') as temporary:
        temporary = Path(temporary)
        source = temporary / 'test_propagation.c'
        template = (here / 'test_propagation.c').read_text(encoding='utf-8')
        source.write_text(template.replace('/* PRODUCTION_FUNCTION */', function(text)), encoding='utf-8')
        binary = temporary / ('propagation.exe' if os.name == 'nt' else 'propagation')
        if is_msvc:
            command = compiler + ['/nologo', '/W4', '/WX', '/TC', '/Od', str(source), '/Fe' + str(binary)]
        else:
            command = compiler + ['-std=gnu89', '-O2', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement', '-Wno-int-to-pointer-cast', str(source), '-o', str(binary)]
        subprocess.run(command, cwd=temporary, check=True)
        return subprocess.run([str(binary)], cwd=temporary, check=False, timeout=20).returncode


if __name__ == '__main__':
    raise SystemExit(main())
