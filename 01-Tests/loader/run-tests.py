#!/usr/bin/env python3
"""Run the production loader path routines with isolated NT/RTL test doubles.

The native test uses 16-bit WCHAR and a 64-bit SIZE_T, including ASan/UBSan.
It does not emulate the Windows loader or replace a Windows integration build.
Pass --baseline to demonstrate the x64 conversion regression against git HEAD.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    with tempfile.TemporaryDirectory(prefix="vxkex-loader-") as temporary:
        temporary = Path(temporary)
        for name in ("dllpath.c", "dllnotif.c"):
            if args.baseline and name == "dllnotif.c":
                source = subprocess.check_output(
                    ["git", "show", "HEAD:KexDll/" + name], cwd=root
                ).decode("utf-8-sig")
            else:
                source = (root / "KexDll" / name).read_text(encoding="utf-8-sig")
            # Only the two conversion routines are needed from dllnotif.c;
            # the remaining functions require the real Windows loader/SEH.
            if name == "dllnotif.c":
                source = source[:source.index("STATIC NTSTATUS GetDllFullName(")]
            source = source.replace('#include "buildcfg.h"', "")
            source = source.replace('#include "kexdllp.h"', "")
            (temporary / name).write_text(source)
        binary = temporary / "loader-tests"
        command = [
            os.environ.get("CC", "cc"), "-std=gnu89", "-fshort-wchar",
            "-fno-omit-frame-pointer", "-g", "-O1", "-Wall", "-Wextra",
            "-Werror=incompatible-pointer-types", "-Werror=int-conversion",
            "-Wno-unused-function", "-Wno-sign-compare", "-Wno-unused-parameter",
            "-I", str(temporary), str(here / "test_loader.c"), "-o", str(binary),
        ]
        if not args.no_sanitize:
            command.extend(["-fsanitize=address,undefined", "-fno-pie", "-no-pie"])
        subprocess.run(command, check=True, cwd=root)
        environment = os.environ.copy()
        # Some managed runners disallow LeakSanitizer's /proc inspection.
        # The allocator doubles separately assert that every allocation is freed.
        options = environment.get("ASAN_OPTIONS", "")
        environment["ASAN_OPTIONS"] = options + (":" if options else "") + "detect_leaks=0"
        arguments = [str(binary)] + (["device-only"] if args.baseline else [])
        return subprocess.run(arguments, cwd=root, env=environment).returncode


if __name__ == "__main__":
    raise SystemExit(main())
