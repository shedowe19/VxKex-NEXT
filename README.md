# VxKex NEXT — shedowe19 Edition

Windows API compatibility extensions maintained by **shedowe19**.
This independent edition develops and tests fixes for running selected newer
Windows applications on Windows 7, Windows 8 and Windows 8.1.

[Build and test results](https://github.com/shedowe19/VxKex-NEXT/actions/workflows/build.yml)
| [Releases](https://github.com/shedowe19/VxKex-NEXT/releases)
| [Report an issue](https://github.com/shedowe19/VxKex-NEXT/issues)

## Target platforms

| Windows version | Application architectures |
| --- | --- |
| Windows 7, preferably with SP1 and relevant platform updates | x86 and x64 |
| Windows 8 | x86 and x64 |
| Windows 8.1 | x86 and x64 |

Compatibility depends on the application's version, its dependencies, the
installed Windows updates and the available device drivers. Enabling VxKex NEXT
does not guarantee that every modern application will work on these systems.

The compatibility DLLs load into enabled processes through Windows Image File
Execution Options (IFEO). Selected imports are redirected to implementations or
forwarders for APIs missing from the target Windows version.

## Changes in this edition

- Fix a pointer-size mismatch in x64 DLL device-path conversion.
- Build DLL search paths in temporary buffers, preserve the original on failure,
  and handle an absent or empty `PATH` on newer loaders.
- Give asynchronous DNS operations owned contexts and validated cancellation
  tokens, including completion, timeout and cancellation races.
- Query DEP policy for the requested process and preserve native failure results.
- Correct executable-path quoting, Boolean arguments and hexadecimal spoof flags
  in the configuration tool.
- Add six classic Bluetooth enumeration APIs using the Windows implementation
  in `bthprops.cpl`, with tests for forwarding and failure handling.
- Stop builds and packaging after required-step failures, validate installer
  contents, and run automated checks for `main` and pull requests.

See [MAINTENANCE.md](MAINTENANCE.md) for implementation and verification details.
The [issue review](00-Documentation/Issue_Review_2026-10-31.txt) records 455 upstream
issues and 2,091 accessible comments, with historical fixes, workarounds and
remaining compatibility reports separated.

## Download and install

1. Open this edition's [Build, Release workflow](https://github.com/shedowe19/VxKex-NEXT/actions/workflows/build.yml).
2. Select a **successful** run for `main` and check the commit shown in its summary.
3. Download its `KexSetup_Release_<suffix>.exe` artifact and run the installer.

Published release packages, when available, are listed on this repository's
[Releases page](https://github.com/shedowe19/VxKex-NEXT/releases). Use the Release
installer for normal use; Debug installers are intended for investigating bugs.
Workflow configuration and portable test results alone do not establish a
successful Windows build. Check the actual run result before selecting a build.

## Enable compatibility for an application

1. Right-click the application's executable or shortcut and open **Properties**.
2. Select the **VxKex** tab and enable **VxKex NEXT for this program**.
3. Apply the settings and start the application normally.

Begin with only the main enable option. If the application rejects the Windows
version, try normal version reporting before enabling stronger spoofing options.
Settings apply to the selected program; launchers, helpers and child processes
may need separate diagnosis.

Use **VxKex NEXT Global Settings** to manage configured applications, logging and
MSI integration. The installed application compatibility document provides
additional historical configuration examples; results can differ for newer
application releases.

## Build from source

The supported build path uses the Windows 7.1 SDK toolchain and MSBuild. The
repository includes import libraries and prebuilt inputs required by the solution.
From a Windows SDK command prompt, run these commands in the repository root:

```bat
set VxKex_NEXT_Platform_Toolset=Windows7.1SDK
set VxKex_NEXT_NOPAUSE=1
call "01-Development Utilities\Build_Stable_Version_And_Clean.bat"
```

The script builds Win32 and x64 configurations and packages both Debug and
Release installers as `KexSetup_Debug.exe` and `KexSetup_Release.exe` in the
repository root. A required-step failure stops the build and removes stale
installer outputs. The Windows CI obtains the SDK toolchain using a pinned
archive and verifies its SHA-256 hash before use.

## Regression tests

Portable tests require Python 3 and a C compiler on Linux:

```sh
python3 01-Tests/run-tests.py
```

The suites compile the actual changed C routines with controlled Windows API
substitutes. They exercise CLI parsing, loader path updates and rollback, x64
buffer handling, DEP process handles, asynchronous DNS and Bluetooth forwarding.
Loader tests also use AddressSanitizer and UndefinedBehaviorSanitizer.

Windows contract tests require the Windows 7.1 SDK environment:

```powershell
powershell -NoProfile -File "01-Tests/run-tests.ps1"
```

The Windows workflow also tests build-script and packaging failure handling.
Passing portable tests or compiling on the CI host does not replace integration
tests on Windows 7, 8 and 8.1, including x86/x64 and relevant hardware.

## Known limitations and remaining work

- **MSI:** installation failures remain reported. If an otherwise working MSI
  installer breaks, temporarily disable MSI integration in Global Settings and
  retest. The existing MSI defaults have not been changed by this edition.
- **Chromium sandbox:** the inherited compatibility path automatically adds
  `--no-sandbox` for affected 32-bit Chromium processes on Windows 7. This disables
  Chromium's sandbox for those processes; the existing behavior is unchanged.
- **WinHTTP:** modern proxy-resolver APIs still contain stubs; applications
  depending on PAC/WPAD resolution can fail.
- **API coverage:** DNS-SD, modern graphics/audio APIs and pseudoconsole-dependent
  applications still have gaps. Configuration import/export also remains open.
- **Application regressions:** an application reaching its main window is not a
  complete compatibility test. Check its required features and background helpers.

Report issues in [shedowe19/VxKex-NEXT](https://github.com/shedowe19/VxKex-NEXT/issues)
with the exact Windows version and architecture, application version, VxKex build,
enabled options, reproduction steps and relevant logs. For a regression, include
both the last working and first failing versions when known.

## Maintainer and credits

This edition is maintained by [shedowe19](https://github.com/shedowe19).
Its starting point is VxKex NEXT **1.2.3.2465**, upstream commit
[`5df0008807f5b021625619093589b47710dbea38`](https://github.com/YuZhouRen86/VxKex-NEXT/commit/5df0008807f5b021625619093589b47710dbea38).

Credit belongs to **vxiiduu** for the original VxKex project, **YuZhouRen86** and
contributors for VxKex NEXT, and **ArchitectOfRuin** for the classic Bluetooth
wrappers proposed in [upstream PR #470](https://github.com/YuZhouRen86/VxKex-NEXT/pull/470)
and adapted here. Original author headers and third-party notices are retained.
