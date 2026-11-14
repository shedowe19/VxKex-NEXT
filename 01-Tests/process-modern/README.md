# Process memory priority contracts

`run-tests.py` extracts the production `GetProcessInformation` and
`SetProcessInformation` functions from `KxBase/process.c`. It compiles them with
controlled native NT entry points, and checks the real class39 ABI, supplied
handle, native-error propagation, publication on success, priority bounds,
LastError preservation, and actual Windows memory protection failures.

Run in an MSVC/Windows SDK command environment:

```
python run-tests.py
```

The tests use controlled native entry points. They allocate an owned page to
check read-only/inaccessible pointer handling; they change no actual process
policy. A separate own-process native/integration fixture is required to prove
that the Windows 7 kernel actually applies the requested memory priority.

The implemented public class is `ProcessMemoryPriority` (0), mapped to the
existing kernel `ProcessPagePriority` (39). Valid priorities are 1 through 5;
this controls the memory manager's trimming preference, not CPU scheduling.
The kernel checks the caller's process handle rights. Other public process
information classes remain explicitly unsupported. CPU sets, mitigation policy
enforcement, and process creation attributes are separate unresolved contracts;
these tests make no claims about them.

References:

- [GetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessinformation)
- [SetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation)
- [MEMORY_PRIORITY_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-memory_priority_information)
