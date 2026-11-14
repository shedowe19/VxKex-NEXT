# Completed Boolean async lifetime regression

Run `python run-tests.py` in an architecture-specific VC10/Windows7 SDK command
environment. The test compiles `KxCom/asyncbool.c` with its actual headers, then
executes the same production body with native COM, FTM and GIT. Only the test
translation unit substitutes allocation accounting, a native HSTRING thunk, and
a GIT forwarding decorator. This is not an installed-KxCom/launcher test.

The baseline independently declares the WinRT Boolean ABI and covers completed
result/callback behavior, state after Close, duplicate assignments, reentrancy,
agile operation marshaling and agile/non-agile delegates across apartments.
The additional decorator fences the production-to-GIT call boundary to make
these interleavings deterministic:

- A concurrent Close while get_Completed is in flight.
- Same-thread Close reentry from a GIT lookup.
- Callback completion while a lookup remains pinned.
- Two simultaneous lookups: the first cannot trigger revocation while the
  second is still in flight.

All GIT storage/marshaling/revocation calls still delegate to native COM. The
decorator observes API call overlap; it does not manufacture a COM failure or
claim to expose the native GIT implementation's internal locking. Each retired
cookie must be revoked once after the final reader, outside the object lock.
Object, handler and GIT-wrapper allocation counts must all return to zero.

Microsoft requires callers to coordinate [GetInterfaceFromGlobal and
RevokeInterfaceFromGlobal](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-iglobalinterfacetable-getinterfacefromglobal).
Holding a lock over COM calls can permit apartment-pumping deadlocks; the
implementation instead pins active readers and defers revocation. The object
also aggregates the [free-threaded marshaler](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreatefreethreadedmarshaler).

This implements already-completed Boolean operations, not a pending-operation
scheduler. Win7 lacks some WinRT delegate proxies: a non-agile cross-apartment
get may honestly return E_NOINTERFACE. No raw STA delegate is substituted.
HSTRING/launcher side effects and installed-DLL integration need separate tests.
