# WinHTTP direct fallback regressions

Run `python3 01-Tests/winhttp/run-tests.py`. The runner compiles the actual
`winhttp_open.c` helper and function with controlled WinHTTP/allocator doubles;
it does not send requests or change proxy settings. `CC` selects a portable C
compiler, and `--compiler cl` supports the original SDK x86/x64 environments.

Coverage includes native success/error precedence, the automatic-access and
argument/flag gates, PAC/WPAD/static-proxy/bypass settings, configuration-query
failures with partial allocations, allocated empty strings, cleanup, and actual
fallback results/errors. Allocation counters and an injected free failure check
cleanup and refusal behavior. The tests also keep the existing x86/x64 export
ordinals distinct.

This is a limited fallback for a configuration snapshot that is explicitly direct
in both user and WinHTTP settings. It does not implement automatic PAC/WPAD,
proxy failover or later configuration changes. Existing proxy configurations and
unsupported flags retain their native failure. TLS/certificate/authentication
settings are unchanged. Native DLL builds and target-OS integration tests remain
separate from these portable behavior tests.
