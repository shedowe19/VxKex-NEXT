# Schannel package-discovery integration test

This manual Windows 7 test reproduces the failure that package enumeration
alone missed: enumeration listed the native Schannel packages, while querying
either native name returned `SEC_E_SECPKG_NOT_FOUND` after NEXT redirected the
provider load to KxCryp. Querying the custom KxSChanl package still succeeded.

The fixture uses CLR PInvoke and a delegate obtained from the real loaded
secur32 module. It checks both native names, the custom package, successful
buffer cleanup, and the loaded native provider's `SpUserModeInitialize` export.
It makes no network requests and does not acquire credentials, edit NEXT
configuration, or register a security provider globally.

## Build and run on Windows 7

Use .NET Framework 4 or later. From this directory, build both architectures:

```bat
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe" /nologo /langversion:3 /warn:4 /warnaserror /target:exe /platform:x86 /win32manifest:SspiPackages.manifest /out:SspiPackages.x86.exe SspiPackages.cs
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe" /nologo /langversion:3 /warn:4 /warnaserror /target:exe /platform:x64 /win32manifest:SspiPackages.manifest /out:SspiPackages.x64.exe SspiPackages.cs
```

First run each executable without NEXT activation:

```bat
SspiPackages.x86.exe --native
SspiPackages.x64.exe --native
```

Next enable NEXT for these two exact test executables, retain native Windows
version reporting, and start each as a **fresh process**:

```bat
SspiPackages.x86.exe --vxkex
SspiPackages.x64.exe --vxkex
```

Every required query must return `0x00000000` and the program must exit 0.
The native run requires the two native package names; the NEXT run additionally
requires KxSChanl. The native Windows Schannel module must be loaded and expose
`SpUserModeInitialize`. Do not substitute a successful enumeration for these
query checks. Package state is cached within a process.

Remove only the exact test configurations created for this test when finished.
No Windows DLL replacement or global SSP registration is required. These
integration tests need Windows 7 and the installed components; they are not
part of the portable API-double test suite. Successful metadata queries do not
prove a TLS handshake or traffic filtering.
