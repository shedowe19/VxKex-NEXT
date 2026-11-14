# Windows modern API contract suite

Run on a modern Windows host with Python 3.9+ and the extracted VC10/Windows 7.1
SDK bundle used by the repository build. Both compiler targets run serially;
no SDK installation, registry import, Kex installation or system file copying
is needed. The native NT reference requires a host that supports class58.

From any directory:

```
python "C:\repo\01-Tests\modern-api-suite\run-tests.py" --sdk-root "C:\tools\Win7SDK" --out "C:\test-results\modern-r1"
```

The output directory must be new. Select one target with
`--architectures x86` or `--architectures x64`; select entries with
`--suites process cfgmgr async winrt nt graphics`. `--list` describes their scope
without running anything and also works on non-Windows hosts.

The SDK root must contain `Program Files (x86)/Microsoft Visual Studio 10.0/VC`,
`Program Files/Microsoft SDKs/Windows/v7.1` and its `vcredist` directories, as in
the repository's pinned SDK archive. The runner validates an owned smoke
executable's pointer width and PE machine for each target. It retains compiler
and linker hashes, repository source/header/import-library hashes, command
arguments, raw stdout/stderr, elapsed times, executable hashes and a structured
`summary.json`. Subprocess output is drained while it runs, including the NT
fixture's output larger than a small pipe buffer. A timeout, missing runtime
summary, incomplete native oracle, changed input or failed test fails the run.

| Entry | Public sources/runner | What executes | What it does not establish |
| --- | --- | --- | --- |
| process | `process-modern/run-tests.py` | Production functions with controlled NT entry points, real status translation and protected-pointer cases | Actual Win7 memory-manager policy effects |
| cfgmgr | `cfgmgr-modern/run-tests.py` | Production source, real legacy registration and window/thread, synthetic owned events and lifetime races | Physical PnP delivery or valid physical device-handle coverage |
| async | `async-review/run-tests.py` | Production source, native COM/FTM/GIT, deterministic call fencing and lifetime checks | Installed KxCom/launcher actions or a pending-operation scheduler |
| winrt | `winrt-modern/run-tests.py` | Full production bodies, separate native HSTRING/URI oracle, native URLMON/FTM and STA-to-MTA method/lifetime checks | Installed KxCom, every WinRT class/HSTRING helper, event subscriptions or launcher side effects |
| nt | `nt-modern/HandleTables.c`, `nt-gate-review/make-fixture.py`, `nt-modern/verify-handle-tables.py` | Expanded native owned-process oracle versus the actual production body with only initial class58 availability forced absent; real backend NT calls | Installed Win7 wrapper dispatch, kernel changes, class52 enforcement, restricted targets or all concurrent snapshot races |
| graphics | `graphics-modern/GraphicsContractTests.c` default mode | Production source with mock COM objects, allocation failures and concurrent wrappers | GPU rendering, drivers, installed KxDx, DComp, D3D12 or WDDM upgrade |

The NT entry runs its native reference and candidate with matching architecture
on the same host. It additionally requires complete owner/child controls and
successful native snapshots containing all five known owned handles. An
inventory program exiting 0 or two empty/unsupported logs cannot count as a
passing backport comparison. `nt-query-shim.h` supplies the documented test
translation-unit seam; `nt-query-availability.c` forces only the initial class58
availability result. Both actual project headers and the retained production
body are compiled. No installed DLL is loaded or replaced.

Add `--build-probes` to compile the checked-in `GraphicsModernProbe.cpp` and
`StrictHandleNative.c` into each target's `build-probes` directory. This option
**never executes either probe**. Hardware rendering/presentation and own-process
strict-handle policy observations require separate, explicitly recorded runs.
Installed-Kex modes documented by the individual test folders also remain
separate acceptance. No driver, physical device, guest, JetKVM, browser, display
mode or system policy is changed by this orchestrator.

The existing portable `01-Tests/run-tests.py` and Linux CI list are unchanged.
Do not add these Windows-native runners to that portable list. Individual
process/cfgmgr/async runners remain directly usable in an initialized MSVC SDK
environment. Manual native/hardware commands in the NT/graphics folders remain
useful, while this orchestrator provides their reproducible source-contract
builds and the native NT comparison.

## CI and extension interface

`.github/workflows/windows-modern-api-contracts.yml` runs this suite on
`windows-2022`, prepares the same SHA256-pinned SDK archive as `build.yml`, and
uploads raw evidence even when a suite fails. The SDK is used in-place. Hardware
probes are compiled but never run on the hosted machine. GitHub execution is a
separate validation from local Windows runs; a checked-in workflow alone does
not prove a hosted run has passed.

Additional source contracts should provide a stable
`run-tests.py --compiler <absolute-cl.exe> --keep-output <new-directory>` and a
nonempty, machine-readable success summary. The winrt entry now follows that
interface and checks both retained production bodies and separate native inputs.
A native-only shape oracle is useful evidence but is not a production backport
test. Installed-Kex activation, launcher
side effects and real DComp/hardware tests must remain separate modes and must
not silently become required hardware actions in this default suite.
