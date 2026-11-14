# Propagation cleanup regressions

`python3 01-Tests/propagation/run-tests.py` compiles the actual
`KexpCleanupPropagationRemains` function with controlled native API substitutes.
It copies the real selected instruction bytes into an isolated byte buffer,
checks an adjacent syscall canary, and covers native x86/x64 plus both WOW64
template variants. The legacy 24-byte write crosses a simulated page boundary.

Fault injection verifies that failed protection leaves the hook mapped and
unchanged, and that copying, instruction-cache flushing and protection restoration
finish before freeing the hook with the required zero `MEM_RELEASE` size. Failed
flushing/restoration retains the allocation. These tests execute no real syscall
stub and modify no operating-system DLL.

`--compiler cl` supports the original Windows SDK compiler in x86/x64 toolchain
environments. On other platforms, `CC` or `cc` selects a C compiler. The suite runs without attaching to or changing a Windows process.
