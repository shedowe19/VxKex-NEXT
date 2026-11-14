#!/usr/bin/env python3
"""Compile and run the production CPU set implementation on the build host.

The real cpuset.c body is inserted into a harness that supplies the small set
of Kex-type shims. The test only changes and restores its own affinity.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def between(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default=None)
    parser.add_argument('--keep-output', type=Path, default=None)
    parser.add_argument('--ntdll-lib', default='ntdll.lib')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    source = (root / '01-Extended DLLs/KxBase/cpuset.c').read_text(encoding='utf-8-sig')
    body = between(source, '//<CPU_SET_TEST_BODY>', '//</CPU_SET_TEST_BODY>')
    harness = (here / 'cpuset_test.c').read_text(encoding='utf-8')
    harness = harness.replace('/* CPU_SET_BODY */', body)
    compiler = args.compiler or shutil.which('cl')
    if not compiler:
        raise SystemExit('MSVC compiler not found (initialize the Windows SDK environment)')
    with tempfile.TemporaryDirectory(prefix='vxkex-cpuset-contract-') as temporary:
        directory = args.keep_output.resolve() if args.keep_output else Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        test = directory / 'cpuset_contract.c'
        test.write_text(harness, encoding='utf-8')
        executable = directory / 'cpuset_contract.exe'
        command = [str(compiler), '/nologo', '/W4', '/WX', '/O2', '/MT', '/TC',
                   '/D_WIN32_WINNT=0x0601', '/DWINVER=0x0601', str(test),
                   '/Fo' + str(directory / 'cpuset_contract.obj'),
                   '/Fe' + str(executable), '/link', 'kernel32.lib', args.ntdll_lib]
        subprocess.run(command, cwd=directory, check=True)
        result = subprocess.run([str(executable)], cwd=directory, check=True, timeout=20)
        _ = result


if __name__ == '__main__':
    main()
