/* Windows 7 integration fixture. No registry writes or network traffic. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef VOID (WINAPI *QUERY_INTERRUPT_TIME_PRECISE)(PULONGLONG);

int wmain(void)
{
    HMODULE Core;
    HMODULE Base;
    FARPROC Ordinal;
    union {
        FARPROC Raw;
        QUERY_INTERRUPT_TIME_PRECISE Typed;
    } Function;
    ULONGLONG Before, After, Value, Previous, Start, End;
    DWORD Error;
    unsigned int Index;
    unsigned int BoundsFailures = 0;
    unsigned int MonotonicFailures = 0;
    unsigned int LastErrorFailures = 0;
    const DWORD Sentinel = 0x5a17c39e;

    Core = GetModuleHandleW(L"KexDll.dll");
    Base = GetModuleHandleW(L"KxBase.dll");
    printf("CORE_ACTIVE=%u BASE_ACTIVE=%u POINTER_BITS=%u\n",
        Core != NULL, Base != NULL, (unsigned int)(sizeof(void *) * 8));
    if (!Core || !Base) {
        printf("FAIL: fixture requires actual NEXT activation\n");
        return 2;
    }
    Function.Raw = GetProcAddress(Base, "QueryInterruptTimePrecise");
    Ordinal = GetProcAddress(Base, MAKEINTRESOURCEA(1926));
    if (!Function.Raw || Ordinal != Function.Raw) {
        printf("FAIL: named export missing or ordinal 1926 differs\n");
        return 3;
    }
    Previous = 0;
    for (Index = 0; Index < 10000; ++Index) {
        Before = GetTickCount64();
        Value = ~(ULONGLONG)0;
        SetLastError(Sentinel);
        Function.Typed(&Value);
        Error = GetLastError();
        After = GetTickCount64();
        if (Value < Before * 10000ULL || Value > After * 10000ULL)
            ++BoundsFailures;
        if (Index != 0 && Value < Previous)
            ++MonotonicFailures;
        if (Error != Sentinel)
            ++LastErrorFailures;
        Previous = Value;
    }
    Function.Typed(&Start);
    Sleep(30);
    Function.Typed(&End);
    printf("CALLS=10000 BOUNDS_FAILURES=%u MONOTONIC_FAILURES=%u LASTERROR_FAILURES=%u\n",
        BoundsFailures, MonotonicFailures, LastErrorFailures);
    printf("SLEEP_START_100NS=%I64u SLEEP_END_100NS=%I64u PROGRESSED=%u\n",
        Start, End, End > Start);
    if (BoundsFailures || MonotonicFailures || LastErrorFailures || End <= Start) {
        printf("FAIL: interrupt-time fallback contract\n");
        return 1;
    }
    printf("PASS: VOID WINAPI ABI, name/ordinal, tick bounds, monotonicity, LastError and Sleep progression\n");
    printf("LIMIT: GetTickCount64 resolution; this does not test Windows 10 hardware precision or suspend/resume\n");
    return 0;
}
