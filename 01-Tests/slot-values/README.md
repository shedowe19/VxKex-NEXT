# TLS/FLS value-read compatibility

This is one application-independent API family: thread-local and fiber-local
slot reads. It does not concern network TLS. The two functions delegate to
Windows 7's own `TlsGetValue` and `FlsGetValue`, retain their actual return
values and restore the caller's saved last-error value. There is no browser
name, executable-path check, shadow slot table or fabricated success result.

`TlsGetValue2` is documented by Microsoft as a TLS getter whose calls leave
the thread's last error unchanged. A null result remains ambiguous; callers
cannot infer success from the saved last error. See the
[Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue2).

Microsoft's published
[fibersapi.h](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/fibersapi.h)
declares `PVOID WINAPI FlsGetValue2(DWORD)`. A separate Microsoft Learn
semantics page was not available during this work. Its last-error behavior
was compared directly with the native Windows 11 implementation, on both
architectures, for the bounded cases below. This is an observed native
reference, not a claim that every undocumented edge case is standardized.

The new exports occupy explicit ordinals 1927 and 1928. The existing
`QueryInterruptTimePrecise@1926` and ordinal-1 forwarder anchor remain intact.
Existing names, ordinals, forwarders and empty export slots must stay intact.
The existing general API-set routing already covers the process/thread and
fiber contracts, including their version suffixes.

## Integration fixture

`SlotsProbe.c` owns and releases its slots. It checks value reads with both
zero and nonzero initial last-error values: null and non-null data, the
reserved invalid index, an allocated TLS expansion slot, a fresh child
thread, and an owned fiber. It verifies thread isolation, FLS isolation
between fibers, and TLS sharing within a thread across fiber switches.
The 22 scenarios each run with two initial error values, for 44 comparisons
per architecture. It also checks allocations, setters, thread completion,
fiber conversion and cleanup. It performs no registry or network operations.

Use the Windows 7.1 SDK compiler environment for the intended architecture:

```bat
cl /nologo /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 SlotsProbe.c /FeSlotsProbe.exe /link /SUBSYSTEM:CONSOLE,6.01 /MANIFEST:NO /DYNAMICBASE /NXCOMPAT kernel32.lib
mt -nologo -manifest SlotsProbe.manifest "-outputresource:SlotsProbe.exe;#1"
```

Run `SlotsProbe.exe --native` on a Windows version that exposes both native
APIs, without NEXT activation. Run `SlotsProbe.exe --vxkex` on Windows 7 with
NEXT enabled for that exact fixture executable. The latter checks actual
NEXT activation, both named exports, and their matching ordinals. A missing
API is admission failure 3, not a silently skipped success. Exit 0 requires
all comparisons to pass. Exit 1 reports a failed comparison/assertion; 2 is
invalid mode or activation, 4 allocation failure, and 5 thread timeout.
Remove only the fixture configuration created for your own test afterward.

The final fixture was compiled with VC10/Windows 7.1 SDK for x86 and x64.
On native Windows 11 build 29639, both architectures exposed both functions
and passed all 44 comparisons with NEXT absent. On physical Windows 7 SP1
x64, the exact same R3 fixture also passed 44 comparisons and 79 assertions
in each architecture with NEXT active. The scenario sets, exception results
and saved-last-error outcomes matched the native reference. Both temporary
fixture profiles were removed. A pre-change Windows 7 run correctly refused
to proceed because both new exports were absent.

A fresh diagnostic Nightly process started with these DLLs and rendered an
HTTPS Example Domain page. An initial example.com request returned a
connection reset; subsequent requests to both example.org and example.com
rendered normally in that same diagnostic profile. The cause of the initial
reset was not established. These tests do not establish network filtering.

## Limits

Windows 7 continues to own slot allocation limits, index validation, cleanup
and callbacks. This change does not enlarge the native slot capacities or
provide newer Windows fast-path performance. The fixture does not exhaust
all slots or test every invalid index, callback or DLL-unload interaction.
No general Windows 11 program-compatibility promise follows from two APIs.
