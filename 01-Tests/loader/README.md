# Loader path regression tests

Run from the repository root:

```sh
python3 01-Tests/loader/run-tests.py
```

The runner compiles the actual `dllpath.c` implementation and the two device-path
conversion routines from `dllnotif.c` against isolated NT/RTL doubles. It uses
16-bit `WCHAR`, 64-bit `SIZE_T`, AddressSanitizer and UndefinedBehaviorSanitizer.
`CC` selects the compiler; `--no-sanitize` permits an additional plain C run.

Coverage includes duplicate removal and ordering, unchanged loader state after
allocation/append/shrink failures, preserved cached length and buffer capacity,
missing/empty/existing `PATH`, environment-error rollback, Unicode-length bounds,
and counted symbolic-link targets without a null terminator. Allocator doubles
also check that temporary allocations are released. LeakSanitizer's process
inspection is disabled for compatibility with restricted runners.

Before committing, `--baseline` runs just the device conversion case using
`dllnotif.c` from the current git `HEAD`, to demonstrate the original regression.
This option is useful while `HEAD` still contains the unmodified baseline.

These tests exercise C behavior, not the Windows loader. Build the real DLLs and
test initialization on Windows 7, 8 and 8.1 in x86/x64 configurations separately.
