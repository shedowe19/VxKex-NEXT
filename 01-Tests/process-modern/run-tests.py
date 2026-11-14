#!/usr/bin/env python3
"""Compile the production Get/SetProcessInformation with controlled NT APIs.

Requires the MSVC/Windows SDK command environment. Runs a new owned test process;
the harness does not call native policy setters or change any system setting.
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
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    source = (root / '01-Extended DLLs/KxBase/process.c').read_text(encoding='utf-8-sig')
    header = (root / '00-Common Headers/KxBase.h').read_text(encoding='utf-8-sig')
    declarations = between(header, 'typedef enum _PROCESS_INFORMATION_CLASS {', '\n} TYPEDEF_TYPE_NAME(PROCESS_INFORMATION_CLASS);') + '\n} PROCESS_INFORMATION_CLASS;'
    functions = between(source, 'KXBASEAPI BOOL WINAPI GetProcessInformation(', '\nKXBASEAPI BOOL WINAPI SetProcessDefaultCpuSets(')
    harness = (here / 'process_memory_test.c').read_text(encoding='utf-8')
    harness = harness.replace('/* PROCESS_INFORMATION_CLASS */', declarations).replace('/* PROCESS_INFORMATION_FUNCTIONS */', functions)
    compiler = args.compiler or shutil.which('cl')
    if not compiler:
        raise SystemExit('MSVC compiler not found (initialize the Windows SDK environment)')
    with tempfile.TemporaryDirectory(prefix='vxkex-process-contract-') as temporary:
        directory = args.keep_output.resolve() if args.keep_output else Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        test = directory / 'process_memory_contract.c'
        test.write_text(harness, encoding='utf-8')
        executable = directory / 'process_memory_contract.exe'
        command = [str(compiler), '/nologo', '/W4', '/WX', '/O2', '/MT', '/TC', '/D_WIN32_WINNT=0x0601', '/DWINVER=0x0601', str(test), '/Fo' + str(directory / 'process_memory_contract.obj'), '/Fe' + str(executable), '/link', 'kernel32.lib']
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(executable)], cwd=directory, check=True, timeout=20)


if __name__ == '__main__':
    main()
