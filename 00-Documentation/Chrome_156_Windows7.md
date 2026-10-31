# Chrome 156 on Windows 7

The fixes in this change address two observed failures of the original Google
Chrome x64 156.0.8078.80 binaries on Windows 7 SP1 x64 with VxKex NEXT 1.2.3.2465.
They do not modify Chrome or Windows system binaries.

## Configure the installer and browser

Enable VxKex NEXT for the original full Google installer and `chrome.exe`. Select
**Windows 10** version reporting for both, leave child-process propagation
enabled, and retain application-specific compatibility handling. Strong version
spoofing was not required in the tested configuration.

Use the VxKex NEXT loader when the executable cannot be started directly:

```text
"C:\Program Files\VxKex\VxKexLdr.exe" "<path to the original Google mini-installer>" --do-not-launch-chrome
```

For an existing system-wide Chrome installation, run the installer elevated and
add `--system-level`. A per-user install conflicts with a system-wide install;
the original installer can launch the existing browser when that conflict occurs,
even when `--do-not-launch-chrome` was supplied. That launch is not evidence that
the new version was installed.

For initial browser testing, use a separate profile:

```text
"C:\Program Files\VxKex\VxKexLdr.exe" "C:\Program Files\Google\Chrome\Application\chrome.exe" --user-data-dir="<absolute path to a separate profile>" --no-first-run --no-default-browser-check
```

Start the browser from the normal desktop user context. No `--no-sandbox` option
was used in the successful x64 tests. Existing 32-bit Chromium compatibility
behavior is outside the scope of this change.

### Why Windows 10 reporting matters

This Chrome installer optionally installs a WebNN Windows App Runtime package
when it detects Windows 11 24H2 or later. In the observed failure, NEXT reported
`AppInstallManager` activation success but could not provide `IAppInstallManager6`.
Chrome's subsequent `CHECK_EQ` terminated the installer after copying the browser.

Windows 10 reporting skips that optional Windows 11 path through Chrome's own
version check. Chrome 156 supports Windows 10. The tested repair then completed
with process exit code 0. This setting does not implement the Windows App Runtime
or its WinRT interfaces.

## API corrections

`NCryptCreateClaim` is unavailable on Windows 7. The new eight-argument export
returns `NTE_NOT_SUPPORTED` and leaves all caller outputs untouched. This allows
Chrome's optional key-attestation caller to handle a supported failure result;
it does not create or validate cryptographic attestation claims. The original
KxCryp ordinal base remains 1, with the new export appended at ordinal 226.

Chromium deliberately receives the real NTDLL module handle for native syscall
inspection. Its dynamic lookup of `RtlGetDeviceFamilyInfoEnum` previously bypassed
the KxNt forwarder, although KexDll already implemented the function. The KxBase
wrapper now tries the native lookup first and, only for this exact missing name
on the real NTDLL handle with `ERROR_PROC_NOT_FOUND`, returns the existing
`KexRtlGetDeviceFamilyInfoEnum` provider. Other modules, names, ordinals, native
exports and native failure codes retain their existing behavior.

## Verification and limits

The preceding physical Windows 7 experiment used narrowly patched stock NEXT
DLLs. It confirmed the original Google installer finishing successfully, an
unelevated browser outside the management job, a visible page with an actual
HTTP request and JavaScript callback, and a separate persistent-profile HTTPS
page. All 264 installed browser payload files matched the original Google
payload. Those results establish the observed fixes; they must not be presented
as a runtime test of every newly compiled source build in this repository.

The source regression suite compiles and executes the changed C functions:

```sh
python3 01-Tests/chrome/run-tests.py
```

The aggregate Windows runner also executes these contracts with the Windows 7.1
SDK compiler for x86 and x64. Tests cover native-first resolution, a future native
implementation taking precedence, wrong modules, ordinal calls, missing provider
handling, error preservation, the eight-argument claim ABI and unchanged failure
outputs with memory canaries.

Automatic GoogleUpdater operation, long-duration stability, hardware video,
GPU acceleration, DRM and arbitrary Windows 11 application compatibility are not
established by these checks. The online `ChromeSetup.exe` bootstrapper can fail
inside GoogleUpdater before it reaches the browser's original inner installer;
that separate updater path is not repaired by this change.

Sources for the diagnosed callers and API contract:

- [Chromium device-family lookup](https://github.com/chromium/chromium/blob/156.0.8078.80/base/win/win_util.cc)
- [Chromium optional key attestation](https://github.com/chromium/chromium/blob/156.0.8078.80/crypto/unexportable_key_win.cc)
- [Microsoft NCryptCreateClaim contract](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptcreateclaim)
- [Chromium optional Windows App Runtime installation](https://github.com/chromium/chromium/blob/156.0.8078.80/chrome/installer/setup/install_win_app_runtime.cc)
