# AdGuard 7.22 on Windows 7

This note records a tested AdGuard for Windows 7.22 configuration on Windows 7
SP1 x64 with .NET Framework 4.8 and VxKex NEXT. It describes the Schannel loading
fix and the limits of the observed results. It is not a claim that every
AdGuard release or feature works on Windows 7.

## Version reporting

Enable NEXT and select **Windows 10** version reporting for the original
AdGuard installer. Retain normal Windows elevation and the original installer.

For the installed `Adguard.exe`, `AdguardSvc.exe`, and `Adguard.Tools.exe`, keep
NEXT enabled but select **native Windows version reporting** (`None`, numeric
value 0). Strong version spoofing was not required. Do not carry the installer's
Windows 10 report into the runtime configuration: it selected incompatible
newer-Windows .NET/WCF paths in the tested service.

These are different settings: native version reporting leaves NEXT's API
compatibility enabled. It does not mean disabling NEXT for the service.

A temporary per-user WPF software-rendering override used during diagnosis
was restored to its original absence. It is not part of this patch or an
installation requirement. The already-open GUI was retained after that restore;
a fresh GUI launch with the restored per-user setting was not part of this result.

## Schannel package loading

The original .NET HTTPS failure was `SEC_E_SECPKG_NOT_FOUND` in
`SSPIWrapper.AcquireCredentialsHandle`. Native Windows processes could query
both `Schannel` and `Microsoft Unified Security Protocol Provider` successfully.
In an affected NEXT process, package enumeration still listed both names and
`KxSChanl`, but queries for the two native names failed. The custom package query
succeeded. This was a per-process routing failure, not a missing global Schannel
registration.

SSPICLI loads Schannel as a security provider and requests
`SpUserModeInitialize`. The general `schannel -> KxCryp` mapping instead supplied
KxCryp, which does not implement that provider entry point. That general mapping
has a separate purpose: callers such as WinInet load the Schannel namespace for
secur32-style functions.

KxBase now preserves the native Schannel load only when the original caller is
Windows SSPICLI and the requested name is Schannel. Absolute-path handling is
restricted to Windows paths. All four LoadLibrary variants retain the original
caller through the ANSI/Unicode wrappers. The native call keeps its filename,
file handle and flags; the thread's previous rewrite state is restored in a
`finally` block. Native last-error results survive cleanup and the existing
module scan. Other callers retain the general mapping.

No Windows DLL replacement, global SSP registration, certificate-validation
bypass, or fabricated successful SSPI result is involved. Version-5 credential
translation already existed and was not the cause of this failure.

## Validation and limits

The Release builds passed for x86 and x64. All existing KxBase export names,
ordinals, forwarders and export-table slots were retained: 1,897 names for x86
and 1,925 for x64. The additional imports are available in Windows 7 or the
existing KexDll.

On physical Windows 7, fresh CLR4 fixtures on both architectures returned
success for all three package names through direct PInvoke and a delegate
resolved from the native secur32 handle. Native Windows Schannel was loaded;
the custom KxSChanl package remained available. See the
[manual package-discovery regression](../01-Tests/schannel/README.md).

A separate x64 .NET fixture completed HTTPS GETs to `https://example.com/` and
the [official AdGuard test page](https://adguard.com/en/test.html) with HTTP 200
under an exclusive TLS 1.2 policy and normal certificate validation. These
requests did not use credentials, cookies, redirects or a certificate callback.
The TLS version was constrained by policy rather than read from the wire. The
requests establish working .NET HTTPS in that fixture; they do not establish
that a browser's HTTPS traffic was intercepted or filtered by AdGuard.

The installed AdGuard service started and the GUI showed protection enabled
with populated filters. A subsequent service log still reported network-core
events saying `Can't tunnel data without connected remote endpoint`. Their
cause and user-visible effect were not established by the package/HTTPS tests.
The result therefore does not claim error-free tunneling, complete browser
filtering, account activation, or universal application compatibility.
