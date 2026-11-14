# General API compatibility: initial contract coverage

New compatibility work is organized by Windows API contract rather than
application name. The functions below are available to any process activated
with NEXT through the existing general DLL and API-set routes. Their bodies
contain no application-name or executable-path condition. This is a bounded
coverage record, not an inventory of every modern Windows API or a promise
that every Windows 11 program runs on Windows 7.

| API | Implementation and limit | Evidence |
| --- | --- | --- |
| QueryInterruptTimePrecise | Sleep-inclusive boot time in 100 ns units from native GetTickCount64; retains the older clock resolution. | x86/x64 native Windows 7 ABI, ordinal, 10,000-sample bounds/monotonicity/LastError checks and Sleep progression; actual Nightly startup after a full OS restart. |
| TlsGetValue2 | Native thread-local slot read, preserving the incoming LastError value; native Win7 allocation and validation remain in charge. | Microsoft's documented contract; 44 combined TLS/FLS scenarios per architecture on native Win11 and NEXT/Win7, including invalid and expansion indices, null data and fresh threads. |
| FlsGetValue2 | Native fiber-local slot read, preserving the incoming LastError value; no parallel slot table or capacity increase. | Microsoft's published ABI and a bounded comparison with native Win11 x86/x64, including fresh fibers, fiber isolation, invalid indices and both zero/nonzero LastError values. |

The existing process/thread, fiber and realtime API-set mappings already
route these namespaces to KxBase. Export ordinals are explicit additions;
previous names, ordinals, forwarders and empty slots are retained.

## Method for further families

1. Identify the requested API, owning namespace and actual failure boundary.
2. Read the Microsoft contract. Where documentation is incomplete, retain the
   exact native-reference version and declare the cases that were observed.
3. Delegate to Windows 7 primitives when they preserve the contract. Preserve
   real errors and return values; do not add success-only placeholders.
4. Compare architecture-correct ABI, normal cases, errors, ownership and
   cleanup on native reference Windows and on NEXT/Windows 7. Test the function
   directly with independent fixtures before an application smoke test.
5. Check complete export-table preservation, Windows 7 import availability
   and the existing regression suite before publication.

See the [interrupt-time fixture](../01-Tests/interrupt-time/README.md) and the
[slot-value fixture](../01-Tests/slot-values/README.md) for reproduction and
the precise remaining limits. These additions do not replace the OS kernel,
device-driver interfaces or the graphics/runtime components of newer Windows.
