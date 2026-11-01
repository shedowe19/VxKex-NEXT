# Propagation cleanup regression tests

Run from the repository root:

```sh
python3 01-Tests/propagation/run-tests.py
```

The runner extracts the actual `KexpCleanupPropagationRemains` function, including
its production syscall templates, from `KexDll/propagte.c`. It compiles and runs
that function with controlled NT API doubles. The byte arrays are data only;
the tests never execute generated instructions or modify another process.

Each of the four branches is tested with two syscall IDs: native x64, Windows 7
WOW64, newer WOW64, and native x86. The checks verify restored register/transition
instructions, the syscall immediate, `ret 12` on x86, and untouched neighboring
syscall bytes and canaries. The Windows 7 WOW64 entry ends at byte 24; a 27-byte
restoration corrupts the next entry and fails. The native x64 entry occupies
11 code bytes within a 16-byte slot; its hook's padding bytes are not treated as
an adjacent syscall. Missing hooks and absent propagation flags must cause no
memory writes or frees.

To demonstrate failure against an unmodified source snapshot:

```sh
python3 01-Tests/propagation/run-tests.py --source /path/to/original/propagte.c
```

The runner supports `--compiler cl` in Windows SDK 7.1 x86 and x64 compiler
environments, or `CC`/`--compiler` on portable hosts. C89-compatible code uses
fixed-width NT fields and the Windows `__stdcall` convention with MSVC.
Compilers treat warnings as errors. Temporary compiler outputs are removed, and
the test executable runs as a subprocess with a timeout.
Only integer-to-pointer size warnings around the extracted production function
are suppressed: its x86/x64 hook-address branches are compiled together even
when their encoded addresses differ from the host pointer size.

These tests model instruction-byte restoration and API contracts. They do not
establish actual Windows syscall layouts on every OS build, execute WOW64
transitions, reproduce installer registry errors, or test protection failures.
