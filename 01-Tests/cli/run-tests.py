#!/usr/bin/env python3
"""Run the production configuration CLI and boolean parser with Win32 doubles.

The C harness uses 16-bit WCHAR and 32-bit ULONG. It exercises the actual source
functions; path/registry/transaction APIs are isolated test doubles. It does not
replace a Windows 7 SDK build or an elevated configuration integration test.
Pass --baseline to run the same tests against the git HEAD versions.
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
    parser.add_argument("--group", choices=("all", "paths", "flags", "booleans"), default="all")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]

    with tempfile.TemporaryDirectory(prefix="vxkex-cli-") as temporary:
        temporary = Path(temporary)
        for name in ("cmdline.c", "util.c"):
            if args.baseline:
                source = subprocess.check_output(
                    ["git", "show", "HEAD:KexCfg/" + name], cwd=root
                ).decode("utf-8-sig")
            else:
                source = (root / "KexCfg" / name).read_text(encoding="utf-8-sig")
            if name == "util.c":
                # The UI/session helpers need a real Windows desktop. Keep the
                # complete production boolean helper without rewriting its body.
                source = source[source.index("BOOLEAN KexCfgParseBooleanParameter("):]
            else:
                source = source.replace('#include "buildcfg.h"', "")
                source = source.replace('#include "kexcfg.h"', "")
            (temporary / name).write_text(source, encoding="utf-8")

        binary = temporary / "cli-tests"
        command = [
            os.environ.get("CC", "cc"), "-std=gnu89", "-fshort-wchar",
            "-ftrivial-auto-var-init=pattern", "-fno-omit-frame-pointer",
            "-g", "-O1", "-Wall", "-Wextra", "-Wno-unknown-pragmas",
            "-Wno-unused-function", "-Wno-unused-parameter",
            "-Werror=incompatible-pointer-types", "-Werror=int-conversion",
            "-I", str(temporary), str(here / "test_cli.c"), "-o", str(binary),
        ]
        if not args.no_sanitize:
            command.extend(["-fsanitize=address,undefined", "-fno-pie", "-no-pie"])
        subprocess.run(command, check=True, cwd=root)
        environment = os.environ.copy()
        # This harness does not allocate heap memory. LSan's thread enumeration
        # is unavailable in some traced CI containers; keep ASan/UBSan enabled.
        environment["ASAN_OPTIONS"] = ":".join(filter(None, (
            environment.get("ASAN_OPTIONS", ""), "detect_leaks=0"
        )))
        return subprocess.run([str(binary), args.group], cwd=root, env=environment).returncode


if __name__ == "__main__":
    raise SystemExit(main())
