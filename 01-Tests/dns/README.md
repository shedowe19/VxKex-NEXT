# Asynchronous DNS lifetime regression tests

Run from any directory with Python 3, GCC and pthreads:

```sh
python 01-Tests/dns/run-tests.py
python 01-Tests/dns/run-tests.py --iterations 3000
```

The runner compiles the actual `KxNet/gaiasync.c` implementation, replacing only
its two project/Windows header includes with the supplied WinAPI test runtime.
The runtime controls worker submission, blocking resolution and timer dispatch;
real pthreads race completion, timeout and cancellation. The compiler rejects
mixed declarations and statements to preserve the MSVC 2010 C declaration style.
GNU89 and Microsoft extensions allow the project's existing anonymous structs,
bit fields and `//` comments.

Coverage includes invalid, duplicate and completed handles; cancellation before
worker start and during lookup; distinct simultaneous requests; stale-handle
rejection after a new request; timeout/completion/cancel races; a completion
callback which reenters cancellation and frees all caller-owned buffers;
operations without a cancel handle; synchronous passthrough; allocation, timer
creation and submission failure; and token exhaustion without reuse. Every test
joins all worker and timer threads and checks context, timer and result counts.

AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer are enabled by
default. `--no-sanitize` is available for platforms without sanitizer runtimes.
In restricted containers where LeakSanitizer cannot read `/proc`, keep ASan and
UBSan enabled with `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1`; explicit resource
counts still run. This is not a substitute for the normal CI leak check.

These tests validate the production lifetime and handle logic against controlled
WinAPI substitutes. They do not validate Windows ABI details, real DNS provider
behavior or Windows threadpool scheduling. Build the DLL with the Windows 7 SDK
for x86 and x64, and run the separate native Windows contract tests as well.
