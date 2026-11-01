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

The identity check uses the canonical `KexData->SystemDllBase` handle rather than
resolving the NTDLL name again. A nested loader operation can have name rewriting
enabled; a fresh name lookup must not determine whether the caller already holds
the real NTDLL handle. The regression test models this condition. It does not
establish that a particular Chrome call reaches it on a physical Windows 7 PC.

## Online bootstrapper exit code 75044

The online `ChromeSetup.exe` first installs Google Updater. In Chromium
156.0.8078.80, exit code **75044** is `kErrorFailedToRunInstallList`
(`kUpdaterErrorBase + 44`). Google Updater returns it when its installation work
item list fails and is rolled back. That list includes copying updater files,
registry writes and COM server/service registration. The wake-task registration
is explicitly best-effort; its failure alone does not cause this return code.

The message `Failed to determine physical sector size; assuming 4KiB.` is a
separate fallback: the file writer continues using 4096-byte alignment. This
message and the final 75044 line do not identify which installation work item
failed. They are not enough to justify a storage, network or compatibility-API
change.

Keep the Windows 10 reporting and child-process settings described above. To
include the installer work-item details, repeat the failing bootstrapper command
with these additional logging switches, preserving its existing install options:

```text
--enable-logging --vmodule="*/chrome/updater/*=3,*/chrome/installer/util/*=3,*/components/winhttp/*=2,*/components/update_client/*=2"
```

The bootstrapper forwards a supplied `--vmodule` value. Its default logging
already includes updater/component modules, but omits `chrome/installer/util`.
Changing the logging switches enables diagnosis; it does not repair installation.

Google-branded updater logs are normally located at:

- Per-user installation: `%LOCALAPPDATA%\Google\GoogleUpdater\updater.log`
- System-wide installation on x64 Windows:
  `%ProgramFiles(x86)%\Google\GoogleUpdater\updater.log`

Preserve the log immediately after reproducing the failure, including the lines
before `item execution failed` and `Install failed, rolling back...`. If the
install scope changed after elevation, inspect both locations. The useful input
is the updater log, not just the metainstaller's final exit-code line. Inspect
paths and identifiers for private information before sharing the log.

The original browser inner installer used in the preceding Windows 7 experiment
avoids this updater setup path. An online bootstrapper or an outer standalone
package may still run Google Updater; renaming either executable does not turn it
into the browser's inner installer. No automatic updater repair is claimed here.

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
- [Chromium updater error constants](https://github.com/chromium/chromium/blob/156.0.8078.80/chrome/updater/constants.h#L523)
- [Chromium updater installation and rollback](https://github.com/chromium/chromium/blob/156.0.8078.80/chrome/updater/win/setup/win_setup.cc#L152)
- [Chromium sector-size fallback](https://github.com/chromium/chromium/blob/156.0.8078.80/chrome/installer/util/unbuffered_file_writer.cc#L99)
- [Chromium work-item failure logging](https://github.com/chromium/chromium/blob/156.0.8078.80/chrome/installer/util/work_item_list.cc)
- [Chromium updater log path](https://github.com/chromium/chromium/blob/156.0.8078.80/chrome/updater/util/path_util.cc)
