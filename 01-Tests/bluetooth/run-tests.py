#!/usr/bin/env python3
"""Compile the production Bluetooth wrappers and exercise their API forwarding."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main():
    test_dir = Path(__file__).resolve().parent
    source = test_dir.parents[1] / "01-Extended DLLs" / "KxUser" / "blutooth.c"
    compiler = shlex.split(os.environ.get("CC", "cc"))

    with tempfile.TemporaryDirectory(prefix="vxkex-bluetooth-") as temp:
        build_dir = Path(temp)
        # Copy the unmodified production translation unit beside minimal platform
        # headers. Tests execute that C implementation, not a reimplementation.
        shutil.copyfile(source, build_dir / "blutooth.c")
        shutil.copyfile(test_dir / "mock_windows.h", build_dir / "kxuserp.h")
        (build_dir / "buildcfg.h").write_text("/* Host test configuration. */\n")
        executable = build_dir / "test-bluetooth"
        subprocess.run(
            compiler
            + [
                "-std=gnu89",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wdeclaration-after-statement",
                "-Wno-unused-parameter",
                "-I",
                str(test_dir),
                str(build_dir / "blutooth.c"),
                str(test_dir / "test_bluetooth.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        for case in (
            "forwarding",
            "load-failure-and-retry",
            "missing-exports",
            "native-errors",
        ):
            subprocess.run([str(executable), case], check=True)


if __name__ == "__main__":
    main()
