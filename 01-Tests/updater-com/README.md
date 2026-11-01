# Configured updater COM regression tests

Run from the repository root with Python 3 and a C compiler:

    python3 01-Tests/updater-com/run-tests.py

In an x86 or x64 Windows SDK compiler environment:

    python 01-Tests/updater-com/run-tests.py --compiler cl

The runner compiles the actual helpers from KxCom/updaterfallback.h and the
actual Ext_CoCreateInstance wrapper from firefoxpatch.c. It reuses the existing
Chrome suite's function tokenizer. It does not copy a second implementation of
the fallback. Windows builds retain the SDK's handle types, token structures,
HRESULT width and x86 WINAPI calling convention; VC10/Windows 7.1 SDK is supported.
The portable compiler run checks behavior, not the Windows ABI.

All OS calls are replaced with API doubles. A source-call guard rejects newly
introduced calls until a double or explicitly pure helper is provided. The suite
does not activate real COM objects, open registry keys, query real tokens, launch
or terminate processes, change configuration, or make network requests.

The scenarios exercise native-first results and pointer preservation; unrelated
CLSID/context/aggregation/OS rejection; Medium and impersonation gates; AppID,
RunAs, service and registry redirection rejection; exact target version, service
arguments and explicit VxKex configuration; malformed strings; file identity;
successful real-object handoff before and after a simulated spawn; unchanged
original errors after failed launch or failed retry; retry timeout, child exit,
NULL-object rejection, reentrancy, and resource cleanup.

The fake spawn validates the unchanged registered executable, registered
arguments plus -Embedding, inherited environment/current token, zero creation
flags, no handle inheritance and the expected working directory. The file must
remain held during that call. The mocks fail on leaked or double-closed handles.
Only a non-NULL object supplied by the native COM double can produce success.

These unit tests do not prove RPCSS behavior or successful Chrome installation.
The implemented scope is the tested per-user GoogleUpdater 157.0.8089.0
registration with explicit VxKex Windows 10 configuration. System services,
elevation and other versions remain outside this fallback. A per-user installer
may still refuse an existing system-wide Chrome installation.
