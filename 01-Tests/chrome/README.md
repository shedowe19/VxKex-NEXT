# Chrome compatibility regressions

These tests compile and execute the actual `NCryptCreateClaim` and
`Stub_GetProcAddress` production C functions with controlled native API doubles.
The runner extracts their unchanged signatures and bodies from the working tree;
the tests do not maintain replacement implementations of either function.

Run the portable suite with Python 3 and a C compiler:

```sh
python3 01-Tests/chrome/run-tests.py
CC=clang python3 01-Tests/chrome/run-tests.py
```

Run the same C89-compatible tests in each Windows 7.1 SDK compiler environment:

```bat
call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x86
python 01-Tests\chrome\run-tests.py --compiler cl

call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x64
python 01-Tests\chrome\run-tests.py --compiler cl
```

Windows builds use the real SDK types and `WINAPI`, including x86 `__stdcall`.
Portable builds use minimal platform types with fixed 32-bit `DWORD` and
pointer-sized key handles. Linux execution therefore cannot establish the x86
Windows calling convention; the Windows architecture runs cover that boundary.
Both compilers treat warnings as errors. Compiled files live in a temporary
directory, and the documented Python entry point runs both native test executables
as subprocesses with a timeout.

After linking, compare each new KxCryp DLL against an unmodified original DLL of
the same architecture. This uses only Python's standard library and reads PE
exports without loading code:

```sh
python3 01-Tests/chrome/check-exports.py original/x64/kxcryp.dll built/x64/kxcryp.dll
python3 01-Tests/chrome/check-exports.py original/x86/kxcryp.dll built/x86/kxcryp.dll
```

The binary check preserves every old name, ordinal and forwarder target, requires
ordinal base1 and permits only the new code export `NCryptCreateClaim@226`.
The regular suite also guards the existing `AcceptSecurityContext@1` source anchor:
MSVC otherwise raises the ordinal base to226 when that is the only explicitly
numbered export, shifting the old automatically assigned exports. The source
guard catches this configuration regression; the post-link comparison verifies
the actual linked result.

`NCryptCreateClaim` coverage:

- Its real public declaration and an independent eight-argument function-pointer
  type must agree with the compiled implementation.
- Repeated calls return `NTE_NOT_SUPPORTED`, including size queries, all-NULL
  optional values, invalid key/pointer values and nonzero flags.
- The claim buffer, surrounding canaries, result-size value and its neighbors,
  input buffer and parameter descriptors remain unchanged on this unsupported
  failure path. No attestation claim or required size is invented.
- The public `NCryptCreateClaim` export must exist at ordinal226.

`Stub_GetProcAddress` coverage:

- A missing Win7 export on the real NTDLL handle resolves the exact existing
  `KexDll!KexRtlGetDeviceFamilyInfoEnum` provider.
- The NTDLL identity comes from the canonical `KexData->SystemDllBase` value.
  A test double models an active nested loader redirection that would return
  KxNt for a fresh NTDLL name lookup. The fallback must still succeed without
  performing any NTDLL name lookup.
- A future native implementation or any other successful native lookup wins
  immediately, with no provider/module lookup or LastError change.
- Ordinals0,1,65535, unknown names, name case, prefixes/suffixes and native errors
  other than127 do not trigger the fallback.
- The same name on another module, a missing canonical NTDLL handle, a missing already
  loaded provider and a missing provider export preserve native failure behavior.
- The provider `GetModuleHandleW` and `GetProcAddress` test doubles
  deliberately overwrite LastError on success/failure. The wrapper must restore
  the original error127 after those checks.
- The public GetProcAddress export must still target the tested wrapper.

These are API contract regressions, not a Windows loader, cryptographic provider,
browser sandbox or end-to-end Chrome test. They do not launch Chrome, load system
DLLs, change registry state, create keys or make network requests. A successful
suite does not imply that every current Chromium feature works on Windows 7.
