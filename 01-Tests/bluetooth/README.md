# Classic Bluetooth forwarding tests

Run on Linux with Python 3 and a C compiler:

```sh
python3 01-Tests/bluetooth/run-tests.py
```

The harness compiles the unchanged production `blutooth.c` with minimal Windows
API stubs, then runs four independent processes. It verifies all six classic
enumeration wrappers: argument and output forwarding, native results and last
errors, module-load failures with retry, missing exports, and reuse of the loaded
system module. The existing GATT stubs are compiled but remain unchanged.

This host test does not verify Windows calling conventions, the system loader,
or Bluetooth hardware. The real DLL must also build for x86 and x64 with the
Windows 7.1 SDK. A Windows integration test should enumerate radios and devices,
check `GetLastError` when no hardware is present, and close every enumeration
handle and radio handle returned by Windows.
