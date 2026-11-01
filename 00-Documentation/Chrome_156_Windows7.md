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
into the browser's inner installer. The propagation repair below addresses one
loader defect affecting the updater; complete automatic updating still needs
target-system verification.

### Updater 157: registry backup 317 and COM activation 0x800700C1

The supplied follow-up log identifies the updater as **157.0.8089.0 x86** on
x64 Windows. This is a separate version from the Chrome 156 browser tested above.
It contains two distinct failure stages:

1. The first per-user setup reports `Setup succeeded`. The installed internal
   COM server then fails activation with `0x800700C1`
   (`HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT)`).
2. Subsequent setup attempts fail while backing up value 0 in
   `HKCU\Software\Classes\CLSID\{6E1E5C99-BC66-5838-926C-B47FC5A1B44A}\LocalServer32`.
   Chromium's `RegEnumValueW` call returns **317**, so the registry backup fails
   and setup rolls back with **75044**. This still occurs after changing version
   reporting from Windows 11 to Windows 10.

Error 317 is `ERROR_MR_MID_NOT_FOUND`. Windows can return it when an NTSTATUS
cannot be translated to a Win32 error. The backup caller already uses valid
name/data buffers and handles `ERROR_MORE_DATA`; a one-WCHAR name buffer is valid
for an unnamed value.

#### Propagation overwrite and repair

The later target-system investigation reproduced the failure with an ordinary
user token while an elevated start succeeded. It then identified a three-byte
overwrite during cleanup of the propagation hook in the 32-bit child process.
The source independently confirms the incorrect write length in
`KexpCleanupPropagationRemains`' legacy WOW64 branch (`build < 8102`):

- The original Win7 WOW64 syscall entry occupies 24 bytes.
- The cleanup template used five-byte `mov ecx,0` where the existing legacy hook
  uses two-byte `xor ecx,ecx`, producing a 27-byte replacement.
- Its final `C2 0C 00` (`ret 12`) therefore landed at offsets 24 through 26,
  beyond the syscall entry, corrupting the adjacent entry point.

Cleanup now uses the two-byte XOR, restoring exactly 24 bytes. The syscall ID,
argument pointer, WOW64 transition and stack cleanup remain intact. Native x86,
modern WOW64 and x64 templates are unchanged. The identity of the next function
depends on the target NTDLL layout; that identity is not inferred from C source
alone.

The regression compiles and executes the actual cleanup routine with bounded
memory/API doubles. It checks independently expected bytes, write sizes and
unchanged adjacent entry points across all four template variants. The old
source fails with a 27-byte write into the 24-byte legacy entry; the repaired
source passes. SDK runs compile and execute the same tests in x86 and x64.

```sh
python3 01-Tests/propagation/run-tests.py
```

This repairs the propagation memory corruption rather than changing registry
permissions, suppressing error 317 or deleting the existing COM registration.
The target updater must be retested using the newly built DLLs with ordinary
user rights. Elevation is not the compatibility fix.

Earlier successful CI checks covered standalone API contracts and synthetic
registry fixtures, not the affected propagated child on Windows 7. The native
diagnostic script's exit code 0 and `COLLECTION_COMPLETE=TRUE` mean that a report
was created, even when an API error is recorded. They do not prove that the
child process, registry API or updater installation succeeded.

#### Retest COM activation after repairing propagation

The earlier COM error 193 must be retested after the overwrite is repaired.
A local server started by COM/SCM need not be a child of the VxKex-enabled
installer. VxKex propagation and its parent process subsystem-version bypass do
not automatically cover that start route.
The supplied original `ChromeSetup.exe` was subsequently unpacked without
execution. Its embedded `bin/updater.exe` is version 157.0.8089.0, x86 PE32, with
OS and subsystem versions **10.0**. This confirms that the payload requires a
subsystem-version bypass on Windows 7. An unmodified COM launch path can reject
it before VxKex is loaded, matching the earlier 193 failure. Whether the installed
copy is byte-identical and which process rejects it still need target-machine
verification. A 32-bit COM local server on x64 Windows is supported; bitness alone
is not the explanation.

Input fingerprints for reproducing this inspection:

- Original setup: 13,283,768 bytes,
  SHA-256 `f3e7c109ea411e760c4dcb1b9bf1dd0b67bbed303ba5521ba87456a873524b3b`.
- Embedded updater: 7,815,320 bytes,
  SHA-256 `39c3ba6a806eb7486e25f6717fc7b9946cf57989dc894bc7513d1d2cfe253b60`.

Both files were read without modification or execution. No patched Google
installer is distributed by this change. To compare the installed copy, run:

```bat
certutil -hashfile "%LOCALAPPDATA%\Google\GoogleUpdater\157.0.8089.0\updater.exe" SHA256
```

#### Optional diagnostics if registry errors remain

Download [Inspect_GoogleUpdater.ps1](../01-Development%20Utilities/Inspect_GoogleUpdater.ps1)
to the Downloads folder. Run the following in a normal Command Prompt on the
same user account that experienced the per-user installation failure:

```bat
"%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%USERPROFILE%\Downloads\Inspect_GoogleUpdater.ps1"
```

The report is saved as `VxKex-GoogleUpdater-Diagnostic.txt` on the desktop. Share
that report after checking its displayed paths and command strings. The script
reads the affected HKCU key in both registry views and compares Win32 enumeration
and value queries with raw NT enumeration statuses. It records the registered
default COM command as text, without executing it or opening that executable.
It writes only its report: no registry entries are changed, no COM server is
activated, no updater is executed and nothing is uploaded.

The script targets Windows PowerShell 2.0/.NET 3.5-compatible syntax and APIs.
CI exercises the documented script entry point with synthetic registry values
in x86 and x64 Windows PowerShell 5.1. An actual Windows 7/PowerShell 2.0 run is
not established by those checks. A fresh PowerShell
process measures a native baseline; it does not reproduce the updater's VxKex
module state automatically. The report records whether KexDll is loaded.

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

Automatic GoogleUpdater operation after the propagation repair, long-duration stability, hardware video,
GPU acceleration, DRM and arbitrary Windows 11 application compatibility are not
established by these checks. The online `ChromeSetup.exe` bootstrapper can fail
inside GoogleUpdater before it reaches the browser's original inner installer;
the full updater path still needs a fresh target-system test.

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
- [Updater 157 registry backup caller](https://github.com/chromium/chromium/blob/157.0.8089.0/chrome/installer/util/registry_key_backup.cc#L148)
- [Updater 157 per-user COM server registration](https://github.com/chromium/chromium/blob/157.0.8089.0/chrome/updater/win/setup/setup_util.cc#L335)
- [Microsoft NTSTATUS conversion contract](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-rtlntstatustodoserror)
