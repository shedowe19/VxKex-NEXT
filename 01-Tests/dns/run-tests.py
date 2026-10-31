#!/usr/bin/env python3
"""Compile the production DNS implementation against a controllable WinAPI runtime."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iterations', type=int, default=300)
    parser.add_argument('--no-sanitize', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    source = here.parent.parent / '01-Extended DLLs' / 'KxNet' / 'gaiasync.c'
    implementation = source.read_text(encoding='utf-8-sig')
    # Substitute only the Windows SDK/project includes. All production functions,
    # including allocation, registry, cancellation and timer teardown, are compiled.
    for include in ('#include "buildcfg.h"', '#include "kxnetp.h"'):
        if implementation.count(include) != 1:
            raise RuntimeError('Unexpected production include: ' + include)
        implementation = implementation.replace(include, '')
    unit = '\n'.join([
        (here / 'winapi_stubs.h').read_text(),
        implementation,
        (here / 'lifetime_test.c').read_text(),
    ])
    with tempfile.TemporaryDirectory(prefix='vxkex-dns-test-') as tmp:
        test_source = Path(tmp) / 'dns_test.c'
        executable = Path(tmp) / 'dns_test'
        test_source.write_text(unit)
        command = [os.environ.get('CC', 'cc'), '-std=gnu89', '-fms-extensions',
                   '-Werror=declaration-after-statement',
                   '-Werror=implicit-function-declaration',
                   '-Werror=incompatible-pointer-types',
                   '-g', '-O1', '-pthread', str(test_source), '-o', str(executable)]
        if not args.no_sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        environment = dict(os.environ)
        environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:abort_on_error=1')
        subprocess.run([str(executable), str(args.iterations)], check=True,
                       timeout=120, env=environment)


if __name__ == '__main__':
    main()
