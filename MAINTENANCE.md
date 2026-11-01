# Standalone maintenance

This repository contains an independent copy of the complete VxKex NEXT source
tree at upstream commit `5df0008807f5b021625619093589b47710dbea38`, release
1.2.3.2465. See [UPSTREAM.md](UPSTREAM.md) for provenance. Original source headers,
documentation, import libraries, prebuilt DLLs and debug symbols are preserved.

## Changes in this repository

- Correct the pointer-sized output used by DLL device-path conversion on x64.
- Construct DLL search paths in temporary buffers and publish them only after
  every operation succeeds. Preserve the original path on failure and handle
  an absent or empty `PATH` on newer loaders.
- Give asynchronous DNS requests owned contexts and validated cancellation
  tokens, including cancellation after completion and completion/timeout races.
- Forward the caller's process handle when querying DEP policy and preserve
  failure results without copying uninitialized output.
- Correct quoted/unquoted executable argument handling, hexadecimal strong-spoof
  flags and Boolean argument lengths in the configuration tool.
- Add six classic Bluetooth enumeration exports, forwarded to the real Windows
  system implementation. This follows the work proposed in upstream PR #470.
- Route the missing dynamic `ntdll!RtlGetDeviceFamilyInfoEnum` lookup to the
  existing KexDll implementation while preserving native lookup precedence and
  the real NTDLL handle. Add `NCryptCreateClaim` with `NTE_NOT_SUPPORTED` and
  unchanged failure outputs; preserve existing KxCryp export ordinals.
- Stop builds and packaging when a required step fails, reject incomplete
  installers, and run build and regression checks for `main` and pull requests.

## Verification

The complete initial import was checked against the original Git blob hash of
each of the 539 upstream files, covering 83,021,470 bytes. Subsequent source
changes are separate commits and are covered by the checks below.

Portable behavior tests compile the actual changed C routines with controlled
Windows API substitutes. They cover path updates and failure rollback, x64
buffer handling, DEP process handles, CLI arguments, DNS completion races and
Bluetooth forwarding. Loader tests additionally use AddressSanitizer and UBSan.
Chrome compatibility tests exercise the actual lookup wrapper and eight-argument
claim function, including error preservation, output canaries and ordinal calls.

```sh
python3 01-Tests/run-tests.py
```

The Windows workflow installs the original hash-pinned Windows 7.1 SDK toolchain,
runs x86/x64 mitigation contract tests and build-script failure-injection tests,
as well as the Chrome compatibility contracts with the original SDK compiler,
then builds and packages both Debug and Release installers for both architectures.
Current results are available in
[GitHub Actions](https://github.com/shedowe19/VxKex-NEXT/actions).

Portable API substitutes do not reproduce the Windows loader, kernel, device
drivers or Bluetooth hardware. A successful Windows build does not establish
that every application works on Windows 7, 8 or 8.1. Target-OS integration tests
remain necessary for those claims.

The DEP fix preserves the limits of the native
[GetProcessDEPPolicy API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessdeppolicy),
including `ERROR_NOT_SUPPORTED` for a 64-bit target process. It corrects process
handle and failure propagation; it does not emulate additional DEP policies.

## Issue review and remaining work

The [complete issue review](00-Documentation/Issue_Review_2026-10-31.txt) covers
455 upstream issues and 2,091 accessible comments: 140 issues were open and
315 closed at the review snapshot. It includes a proposed next step for every
issue. GitHub counted seven additional comments that were not accessible even
through individual issue requests. Screenshot and binary attachments were not
systematically examined.

The review is an inventory of reports, workarounds, confirmed historical fixes
and limitations. It does not assert that all 455 issues are fixed here. In
particular, the reported MSI installation failures, application-specific
regressions, WinHTTP proxy implementation, DNS-SD support, log size limits and
configuration import/export still require targeted implementation or reproduction.

Existing MSI integration and Chromium sandbox compatibility defaults are
unchanged. The 32-bit Chromium `--no-sandbox` behavior discussed in upstream
issue #290 remains an explicit outstanding decision.
