# Service propagation contracts

Run `python3 01-Tests/services/run-tests.py`. Use `--compiler cl` in the original
Windows 7.1 SDK x86 or x64 environment; `CC` selects a portable C compiler.
The suite builds five independent executables. It starts no real service,
activates no COM object, queries no real tokens and changes no registry setting.
All operating-system calls are replaced explicitly; the runner rejects new calls
until the shim doubles them. Windows tests retain real SDK structures, handles
and WINAPI conventions. Portable tests model behavior, not the Windows ABI.

The SCM wrappers, service resolver, transactional profile writer and COM class
dispatcher are compiled directly from their production source files. The small
SCM-to-Core bridge function is extracted from its production file. The Core
reader test extracts the actual seven registry-query statements; it does not
claim to exercise the unrelated MSI/PEB path-selection logic.

Coverage includes local versus remote/unknown handle provenance; access rights,
native arguments, BOOL results and LastError; ANSI conversion; handle reuse,
close failures and bounded-table exhaustion; stopped/disabled/shared/driver
gates; malformed configuration buffers and ambiguous executable paths; Windows
directory and final-file alias rejection; held-file cleanup; and exact forwarding
of all seven profile fields. The writer tests check existing explicit and dormant
filters, malformed NUL tails, every write-failure position, commit failure,
rollback and resource ownership. The registry doubles maintain staged versus
committed changes, so a failed operation cannot pass as an atomic commit.

COM tests cover registry views, conflicting activation flags, user overrides,
TreatAs/AutoTreatAs, remote registrations, malformed values, helper failure and
LastError preservation. Source integration guards retain the explicit-server
and aggregation exclusions at the COM callers. The reader checks all seven
DWORD destinations and missing-value behavior; bridge tests verify effective
configuration flags and the unchanged profile pointer.

These contracts do not prove RPCSS/SCM timing, real registry transaction conflict
behavior, a real Windows service start or successful browser installation.
Target-OS integration and existing export-table checks remain separate.

The portable integration contract also pins the nine original Win32 SCM export
aliases/ordinals and checks that Debug/Release Win32 configurations select the
dedicated definition file, while x64 configurations retain the common file.
This catches loss of the inputs that preserve MSVC ordinal allocation; it does
not simulate the linker or replace comparison of the actual built PE exports.
