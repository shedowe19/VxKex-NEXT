# Native class58 dispatch-gate regression

This extends the owned `nt-modern/HandleTables.c` matrix with lengths0–3
combined with misaligned output, unaligned ReturnLength, inaccessible
ReturnLength, and an output aligned to four but not eight bytes. The original
rights, cooperative child, known handles, boundary and terminated-child cases
remain present. The generator verifies the reviewed base source content hash
after normalizing CRLF so a Windows checkout cannot change the pin.

In a VC10/Windows7 SDK architecture-specific command prompt:

```
python make-fixture.py --out HandleTablesGate.c
cl /nologo /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 HandleTablesGate.c /Fe:HandleTablesGate.exe /link /SUBSYSTEM:CONSOLE,6.01 /MANIFEST:NO
```

For an installed NEXT test, add `/DHANDLE_TABLE_KEX_TEST` and choose a separate
executable name. Enable NEXT for that exact test executable through normal Kex
configuration. This variant requires active KexDll and verifies the query
export belongs to it. Run without arguments and capture stdout concurrently
with execution; the expanded output exceeds a small pipe buffer. Compare the
matching native modern-Windows and installed-NEXT logs with
`../nt-modern/verify-handle-tables.py`.

Why the extra cases matter: modern NT checks output alignment when length is
nonzero and probes the optional ReturnLength before rejecting a buffer shorter
than one ULONG. Native Win7 x64 performs these generic checks before rejecting
the unsupported class; the Win7 WOW64 path does not. Testing only a forced
fallback, or only valid pointers, can therefore miss an actual x86 wrapper
regression. Length0 with misaligned output still returns length mismatch, while
length1–3 returns misalignment. An inaccessible ReturnLength returns access
violation even when length is0–3.

The fixture changes only its own and its cooperative child's handles. It
enables no privilege, changes no registry or policy, and exits the owned child
with259 to distinguish termination from a merely pending numeric exit status.
These tests establish the stated bounded matrix, not atomic snapshots under
all concurrent handle mutations or access to restricted/protected processes.
