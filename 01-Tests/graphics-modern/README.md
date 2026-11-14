# Graphics contracts and hardware G1

`GraphicsModernProbe.cpp` is a bounded Direct3D 11 hardware test for x86/x64,
built with Visual C++ 2010 and the Windows 7.1 SDK. The checked-in shader headers
are compiled from `Shader.hlsl` with Microsoft's SDK fxc, profiles `vs_4_0` and
`ps_4_0`. No shader compiler DLL or SDK installation is needed on the test PC.

Run `GraphicsModernProbe.exe --native` with Kex absent, then a distinct configured
copy with `--vxkex`. A mismatch between the requested mode and loaded KexDll is a
failure (exit 2). Hardware creation has no WARP/software fallback; feature level
10.0 or higher is required by these shaders.

Each run creates a small, non-activating HWND and a legacy RGBA8 swapchain:

1. Compile-time vertex/pixel shader bytecode produces a full-screen triangle.
2. Two different constant-buffer colors are drawn over a black clear, copied to
   staging, mapped, and checked at every pixel with the actual row pitch.
3. Each frame is presented to the HWND; `S_OK` is required. An occlusion status
   is not counted as successful presentation.
4. References to the old backbuffer are released, the HWND and swapchain resize
   from 64x64 to 96x80, and both draw/readback/presentation checks repeat.

Exit 0 requires four actual draws, 23,552 exact pixel matches, four `Present`
successes and one successful `ResizeBuffers`. GPU readback polling has a five
second deadline; the harness should additionally set a process timeout to cover
driver calls. There are no display-mode changes, fullscreen transitions or load
loops. A successful API presentation is not a screenshot-based visual check.

`GraphicsContractTests.c` includes the production D2D/backend source functions.
Its default mode runs 57 independent outer-D2D checks plus six missing-backend
contracts (63 total), with no hardware rendering. `D2DProxyContracts.c` can also
run that 57-check component alone. It exercises canonical QI/GetFactory identity,
owner retention after caller Factory release, OOM/native failure, repeated wrap,
four concurrent AddRef/Release workers and final cleanup. Native mock objects have
additional private teardown slots immediately after their public vtables; those
methods are actually invoked. Their native vtables must remain untouched.

`DxgiProxyContracts.c` is the independent DXGI outer-object suite. The central
`modern-api-suite/run-tests.py --suites graphics` builds and requires the
combined D2D/backend suite, this DXGI suite and `D3D11BridgeContracts.c`.
The D3D11 bridge component requires 1,399 checks, 36 controlled native-function
calls, zero failures and no live proxy allocations. The former in-place/CAS/100-check
suite is superseded: its assumptions do not safely cover native private slots.
These source suites do not replace actual installed-DLL or hardware checks.

`D3D11BridgeContracts.c` includes the production DXGI proxies and both D3D11
creation thunks, replacing only the two native API calls with controlled test
callbacks. It verifies known outer/native/foreign/null adapter handling, arguments
and returned outputs, real HRESULT/LastError forwarding, optional outputs, a
caller releasing the outer adapter during the pinned call, and SEH cleanup.
Those 36 callbacks are mock boundaries, not real GPU-device or swapchain calls.

The two reviewed D3D11 exports intentionally change from native forwarders to
direct adapter-unwrapping thunks while preserving each old name and ordinal.
Known KxDx adapters are pinned/unwrapped for those calls; other arguments pass
through to native validation. This is not universal COM unwrapping. Output
interfaces and swapchains are still native; their GetParent/GetAdapter and
factory/output/swapchain identity graphs are not wrapped or certified by these
tests. Enumeration of native outputs is likewise not a claim that every native
API accepts an outer Factory/Adapter. These are explicit integration limits;
real hardware/application regression tests must separately exercise each graph
and boundary before deployment.

The extended D2D Factory2/Device1 path is a safe subset, not a complete D2D1.2
backend. Factory/Device QI returns only interfaces the proxy actually implements;
unknown interfaces fail with null output. Device.GetFactory returns the same
outer Factory IUnknown. Other native resource/context outputs would leak a
different factory/device identity, so their unimplemented proxy methods return
E_NOTIMPL and null outputs. This includes RectangleGeometry, render targets,
base/extended contexts and print controls. Requests for native Factory0/Factory1
remain native and keep their normal resource APIs. Rendering-priority backend
behavior also remains open; no scheduling improvement is claimed.

Additional modes:

```
GraphicsContractTests.exe --native-reference
GraphicsContractTests.exe --installed "C:\absolute\path\to\KxDx.dll"
```

The first mode measures native DXGI feature/flag contracts and requires Kex to
be absent. The second requires Kex to be active and tests the actual DLL loaded
from the supplied path, including repeat adapter enumeration, separate D2D
factories/devices, safe unsupported Context1/RectangleGeometry calls and the
Device.GetFactory identity roundtrip after the caller releases its Factory.
Every original physical DXGI/D2D check remains present. It creates one real
hardware BGRA D3D11 device for D2D interop, with no software fallback. Select the DLL
matching the executable architecture. The default source tests do not prove that
an installed DLL was replaced; the `--installed` mode provides that separate check.

A private candidate outside the installed KexDir can have its own native imports
rewritten through the old installed KxDx, invalidating the comparison. For an
isolated run, the controller must establish real import/function ownership before
counting the result. A distinct Kx-prefixed diagnostic filename under KexDir
receives the normal component exemption; this does not authorize replacing an
existing DLL or globally activating the candidate. Process deadlines remain
necessary around native driver/COM teardown.

For a Win7 SDK compiler environment, the hardware probe can be built with:

```
cl /nologo /W4 /WX /O2 /MT /EHsc /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 GraphicsModernProbe.cpp /link /SUBSYSTEM:CONSOLE,6.01 /DYNAMICBASE /NXCOMPAT kernel32.lib user32.lib gdi32.lib d3d11.lib dxgi.lib
```

The source-inclusion contract harness also needs the repository's common headers
and architecture-specific import-library directory. Its standalone link overrides
the repository DLL entrypoint/subsystem and excludes unused Kex libraries; the
isolated integration build records the full command. It does not link or execute
the complete KxDx project on the build host.

## Boundaries

These tests exercise the existing hardware D3D11/HWND path. They do not implement
or certify DirectComposition, NT shared GPU handles, modern flip/composition
swapchains, D3D12, 11-on-12, video decoding, HDR, cross-process graphics or a WDDM
upgrade. Microsoft documents Windows 7 Platform Update as WDDM 1.1 with partial
11.1/1.2 interfaces and explicitly excludes DComp and NT-handle resources:
[Platform Update for Windows 7](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7).

An adapter interface extension whose newer operation returns an unsupported
HRESULT is not evidence of that operation's implementation. The unavailable
power/performance GPU ordering policy is reported as unsupported; unspecified
preference uses native enumeration order, as documented by
[EnumAdapterByGpuPreference](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference).
