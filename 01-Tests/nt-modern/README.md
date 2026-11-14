# NT process handle-table contracts

`HandleTables.c` operates on itself and one cooperative copy of itself. Both
processes create three unnamed Events and two unnamed Jobs. The child publishes
its own handle values through an inherited private mapping and waits at most
30 seconds. It deliberately exits with **259**, so an implementation cannot
mistake `ExitStatus == STATUS_PENDING` for proof that a process is still live.
The parent exits with zero when the diagnostic completed. This is not an API
support or correctness verdict.

The default build refuses loaded Kex DLLs and verifies the reference functions
belong to native NTDLL. Build it separately for x86 and x64 in the corresponding
VC developer prompt:

```
cl /nologo /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 HandleTables.c /Fe:HandleTablesNative.exe /link /SUBSYSTEM:CONSOLE,6.01 /MANIFEST:NO
```

For an **installed NEXT** test, build the same source with
`/DHANDLE_TABLE_KEX_TEST` and a different executable name. Enable NEXT for that
exact test executable through the normal Kex configuration. This variant
requires an already loaded KexDll and verifies that its query entry point is
`KexDll!Ext_NtQueryInformationProcess`; it does not load or replace system DLLs.
Run either variant with no arguments and capture stdout. Keep the matching
architecture when comparing a native modern-Windows reference to a candidate:

```
python verify-handle-tables.py native.log next.log --out comparison.json
```

The verifier checks error status and exception parity, four-byte truncation,
unaligned optional ReturnLength, complete output-buffer probing across a page
boundary, readonly output rejection, canaries, LastError preservation, sorting
and uniqueness, five independently known owned handles, access-right variants,
wrong-type/invalid handles and the exited child. Absolute table sizes from
different processes are not required to match. An observation-only run on
native Windows 7 reports unsupported class 58; that is not a modern reference.

## Implementation boundary

The class-58 fallback is used only when the native query reports
`STATUS_INVALID_INFO_CLASS`. A supported native implementation remains in use.
The measured native format is a **ULONG array even for x64**, not an array of
pointer-sized HANDLEs or a header followed by entries. The reference requires
both `PROCESS_QUERY_INFORMATION` and `PROCESS_DUP_HANDLE`.

The fallback keeps a duplicate with the original granted access, obtains a
real `SystemExtendedHandleInformation` snapshot, filters only the exact target
PID, hides its own temporary handle, and returns numeric handle values. It
checks the referenced process's exit status **and exit time** before and after
the snapshot. No privilege is enabled and no kernel object address is returned.
Allocations are limited to 64 MiB and twelve attempts; a denied or failed native
backend remains an error. Concurrent handle changes can still make a snapshot
stale immediately. The test matrix does not prove equivalence for all races,
restricted tokens, protected processes or arbitrary handle populations.

## Strict-handle investigation

`StrictHandleNative.c` accepts exactly one of `baseline`, `trace`, `checking`,
`policy`, or `public-policy`. Each invocation modifies only its own disposable
process. It uses original native NtClose and the real CloseHandle, catches
their observed exceptions, and never manufactures an exception. It reads but
does not modify global flags or the registry. The default variant refuses Kex.

Fresh native observations show that reversible `ProcessHandleTracing` (class
32) enables actual invalid-handle exceptions, including on the tested Windows 7
system. This is **not** an implementation of irreversible strict-handle policy,
class-52 state, process-wide syscall mediation, or the entire modern mitigation
contract. Class 52 remains a native query in the production NT wrapper. Existing
public mitigation compatibility behavior must be assessed separately.

Microsoft's [NtQueryInformationProcess documentation](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess)
warns that this native interface can change. It does not document this class-58
format; these tests are an explicit empirical reference for that contract.
Global [bad-handle detection](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/enable-bad-handles-detection)
is a separate system-wide mechanism and is not changed by these tests.
