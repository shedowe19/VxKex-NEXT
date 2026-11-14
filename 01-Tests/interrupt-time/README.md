# Windows 7 interrupt-time integration regression

Brave Nightly 158.1.102.6 can terminate during its delayed import of
`QueryInterruptTimePrecise` from `api-ms-win-core-realtime-l1-1-1.dll` when
NEXT maps that contract to KxBase but the function is absent. A recorded
Windows 7 crash and NEXT procedure-lookup log identified the exact function;
the native fixture also failed before the export was added.

The fallback returns `GetTickCount64() * 10000ULL`: elapsed boot time,
including sleep, expressed in 100 ns units. Its resolution remains that of
GetTickCount64. It does not supply Windows 10's hardware-based precision.
This limitation follows the distinction described in Microsoft's
[QueryInterruptTimePrecise documentation](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryinterrupttimeprecise).

Build `InterruptTimeProbe.c` with the Windows 7 SDK for x86 and x64 and embed
the adjacent manifest. It uses only Windows 7 APIs and makes no registry
changes or network requests. Enable NEXT for the exact two test executables,
retain native version reporting and start each as a fresh process. PowerShell
or the shell used to configure them does not need NEXT activation.

Expected result: exit 0, active KexDll and KxBase, the same real function
address for the name and ordinal 1926, 10,000 monotonic samples bounded by
native tick counts, preserved LastError and advancement across Sleep(30).
Remove only the temporary NEXT profiles created for these fixtures afterward.

Both architecture fixtures passed these checks on physical Windows 7 SP1 x64.
The test does not establish Windows 10 precision or suspend/resume behavior.
After a complete Windows 7 restart, there were no running Nightly processes.
Opening the ordinary desktop shortcut through the Medium Explorer shell
then started the updated Nightly with the existing normal user profile and
rendered a restored page. The new boot timestamp, installed DLL hashes and
unchanged desktop, Start Menu and taskbar launcher commands were verified.
This startup test does not establish compatibility with arbitrary programs
or Windows 10 hardware timer precision.

The existing AccessCheck forwarder is explicitly anchored at ordinal 1 so
the linker retains the old export base when adding ordinal 1926. Complete
x86/x64 table comparison preserved every previous named export, ordinal and
forwarder; the new timing function is the only addition.
