#!/usr/bin/env python3
"""Compile and execute the actual mitigation function with controlled native APIs."""
import argparse
import os
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
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    header = (root / '00-Common Headers/KxBase.h').read_text(encoding='utf-8-sig')
    declarations = between(header, 'typedef enum _PROCESS_MITIGATION_POLICY {', '\nDECLARE_HANDLE(HPSS);')
    source = (root / '01-Extended DLLs/KxBase/process.c').read_text(encoding='utf-8-sig')
    function = between(source, 'KXBASEAPI BOOL WINAPI GetProcessMitigationPolicy(', '\nKXBASEAPI BOOL WINAPI IsProcessCritical(')
    harness = (here / 'mitigation_test.c').read_text()
    harness = harness.replace('/* POLICY_DECLARATIONS */', declarations).replace('/* MITIGATION_FUNCTION */', function)
    compiler = args.compiler or shutil.which('gcc') or shutil.which('cc')
    if not compiler:
        raise SystemExit('C compiler not found')
    with tempfile.TemporaryDirectory(prefix='vxkex-contracts-') as directory:
        directory = Path(directory)
        test = directory / 'mitigation_test.c'
        test.write_text(harness, encoding='utf-8')
        executable = directory / ('mitigation_test.exe' if os.name == 'nt' else 'mitigation_test')
        if Path(compiler).stem.lower() == 'cl':
            command = [compiler, '/nologo', '/W4', '/WX', '/TC', str(test), '/Fe' + str(executable)]
        else:
            command = [compiler, '-std=gnu89', '-Wall', '-Wextra', '-Werror', str(test), '-o', str(executable)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(executable)], cwd=directory, check=True, timeout=20)


if __name__ == '__main__':
    main()
