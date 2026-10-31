#!/usr/bin/env python3
"""Run portable regression tests as independent subprocesses."""
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent
    for suite in ('contracts', 'cli', 'loader', 'dns', 'bluetooth'):
        subprocess.run([sys.executable, str(root / suite / 'run-tests.py')], check=True)
    print('All portable regression suites passed.', flush=True)


if __name__ == '__main__':
    main()
