# WinRT source contracts with real Windows backends

Run in an architecture-specific VC10/Windows7.1 SDK command environment:

```
python run-tests.py --compiler C:\tools\VC\bin\cl.exe --keep-output C:\results\winrt-x86
```

Or use the central Windows suite with `--suites winrt`. The runner compiles the
complete production bodies of winrt/actfact/rofactry/roinit/uri/glprstat/uisettings/
uiview/asyncbool/launcher into an isolated executable. The runner never rewrites
the included production bodies.
Only logging, allocation accounting/failure injection and a native FTM forwarding
decorator are substituted. Ole32, URLMON, native system-color/locale queries and
FTM marshaling are real. No Kex DLL is loaded/injected or installed, and no
launcher method, browser, registry, hardware device or system policy is changed.

The DLL headers carry linker directives; the runner explicitly selects the CRT
test entry point and fixes the executable subsystem to Console with SDK editbin.
Shared headers/legacy bodies retain the project's warning level3, and test code
uses /W4 /WX. No warning suppression hides the former uninitialized comparison
flag; its production initializer is exercised against the native matrix.

Covered contracts:

- Five exact runtime class names, rejection of unknown/unrelated IIDs, metadata
  arrays whose advertised interfaces actually query, factory/statics identity,
  class-specific activation, and embedded-NUL class-ID rejection.
- Production HSTRING ownership; 25 NULL/empty/nonempty/case ordinal comparisons
  against separately created native Windows HSTRING inputs. The native opaque
  handles use only dynamically resolved native APIs; candidate handles use only
  production winrt.c. These domains are never interchanged.
- Native and candidate URI construction/relative combination with embedded NUL,
  which intentionally follows the native URL parser's prefix truncation.
  Foreign Equals uses a minimal public-property test double whose private payload
  differs from the implementation; comparison still executes real URLMON code.
- Typified IVectorView<HSTRING> IID2f13..., allocated output arrays, actual GetMany
  counts, NULL output buffers with zero capacity, boundaries and element lifetime.
- Real STA-to-MTA streams for all five factories plus URI, UISettings3 and vector.
  The creator's reference is released before the receiver unmarshals. Receiver
  methods read/compose URI values, read actual system color and consume vector
  HSTRING results, then release the final per-instance payload and native FTM.
- Four concurrent cached-FTM constructors yield one retained winner and three
  freed losers. Injected FTM creation failure leaves cache initialization retryable
  and URI/vector constructors unpublished with source allocations cleaned up.

All tracked source allocations and per-instance marshalers must be released.
Six exercised immutable singleton cache marshalers intentionally live until
process exit; the summary reports them separately rather than claiming no cache
allocation exists. The decorator delegates actual IMarshal behavior to native
FTM; it does not fabricate marshaling success.

This is a bounded source/runtime suite on a modern Windows host with native
COMBASE HSTRING and Windows.Foundation.Uri available. It is not an installed
KxCom/Win7 activation test or a full HSTRING audit. Uncalled older HSTRING helpers,
UISettings event subscriptions/fixed tokens, additional WinRT classes, pending
async scheduling and launcher side effects are not certified by these checks.
