#include "test_platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Rename calls only; the production wrapper body below remains unchanged. */
#define GetProcAddress MockGetProcAddress
#define GetModuleHandleW MockGetModuleHandleW
#define GetLastError MockGetLastError
#define SetLastError MockSetLastError

static int NtdllToken, ProviderToken, OtherToken, RedirectedNtdllToken;
#define NTDLL_MODULE ((HMODULE)&NtdllToken)
#define PROVIDER_MODULE ((HMODULE)&ProviderToken)
#define OTHER_MODULE ((HMODULE)&OtherToken)
#define REDIRECTED_NTDLL_MODULE ((HMODULE)&RedirectedNtdllToken)
/* Only the production field used by this wrapper is needed in the API double. */
static struct { void *SystemDllBase; } ProcessData;
#define KexData (&ProcessData)
static FARPROC NativeResult, ProviderResult;
static ULONG NativeError, LastError;
static unsigned int NativeCalls, NtdllQueries, ProviderQueries;
static int NtdllPresent, ProviderPresent, RedirectNtdll;
static HMODULE ExpectedModule;
static PCSTR ExpectedName;

static INT_PTR WINAPI NativeFunction(void) { return 11; }
static INT_PTR WINAPI ProviderFunction(void) { return 22; }

static FARPROC WINAPI MockGetProcAddress(HMODULE module, PCSTR name)
{
    ++NativeCalls;
    if (NativeCalls == 1) {
        assert(module == ExpectedModule && name == ExpectedName);
        LastError = NativeError;
        return NativeResult;
    }
    assert(NativeCalls == 2);
    assert(module == PROVIDER_MODULE);
    assert(strcmp(name, "KexRtlGetDeviceFamilyInfoEnum") == 0);
    LastError = 703; /* Additional lookups may overwrite LastError on success. */
    return ProviderResult;
}

static HMODULE WINAPI MockGetModuleHandleW(const wchar_t *name)
{
    if (wcscmp(name, L"ntdll.dll") == 0) {
        ++NtdllQueries;
        LastError = 611;
        if (RedirectNtdll) return REDIRECTED_NTDLL_MODULE;
        return NtdllPresent ? NTDLL_MODULE : NULL;
    }
    assert(wcscmp(name, L"KexDll.dll") == 0);
    ++ProviderQueries;
    LastError = 612;
    return ProviderPresent ? PROVIDER_MODULE : NULL;
}

static ULONG WINAPI MockGetLastError(void) { return LastError; }
static void WINAPI MockSetLastError(ULONG error) { LastError = error; }

/* PRODUCTION_FUNCTION */

static void Reset(HMODULE module, PCSTR name)
{
    ExpectedModule = module;
    ExpectedName = name;
    NativeResult = NULL;
    NativeError = ERROR_PROC_NOT_FOUND;
    ProviderResult = ProviderFunction;
    LastError = 999;
    NativeCalls = NtdllQueries = ProviderQueries = 0;
    NtdllPresent = ProviderPresent = 1;
    RedirectNtdll = 0;
    ProcessData.SystemDllBase = (void *)NTDLL_MODULE;
}

static void NoFallback(HMODULE module, PCSTR name, ULONG error)
{
    Reset(module, name);
    NativeError = error;
    assert(Stub_GetProcAddress(module, name) == NULL);
    assert(NativeCalls == 1 && ProviderQueries == 0 && NtdllQueries == 0);
    assert(LastError == error);
}

int main(void)
{
    PCSTR target;
    target = "RtlGetDeviceFamilyInfoEnum";

    /* A nested loader call can redirect a fresh NTDLL name query to KxNt.
       The canonical native handle must still receive the compatibility export. */
    Reset(NTDLL_MODULE, target);
    RedirectNtdll = 1;
    assert(Stub_GetProcAddress(NTDLL_MODULE, target) == ProviderFunction);
    assert(NativeCalls == 2 && NtdllQueries == 0 && ProviderQueries == 1);
    assert(LastError == ERROR_PROC_NOT_FOUND);

    /* Win7 failure on real NTDLL resolves the already loaded existing provider. */
    Reset(NTDLL_MODULE, target);
    assert(Stub_GetProcAddress(NTDLL_MODULE, target) == ProviderFunction);
    assert(NativeCalls == 2 && NtdllQueries == 0 && ProviderQueries == 1);
    assert(LastError == ERROR_PROC_NOT_FOUND);

    /* A future native implementation must always win over the compatibility one. */
    Reset(NTDLL_MODULE, target);
    NativeResult = NativeFunction;
    NativeError = 0x5151;
    RedirectNtdll = 1;
    assert(Stub_GetProcAddress(NTDLL_MODULE, target) == NativeFunction);
    assert(NativeCalls == 1 && NtdllQueries == 0 && ProviderQueries == 0);
    assert(LastError == 0x5151);

    Reset(OTHER_MODULE, "NtClose");
    NativeResult = NativeFunction;
    NativeError = 0x6161;
    assert(Stub_GetProcAddress(OTHER_MODULE, ExpectedName) == NativeFunction);
    assert(NativeCalls == 1 && NtdllQueries == 0 && ProviderQueries == 0);
    assert(LastError == 0x6161);

    NoFallback(NTDLL_MODULE, (PCSTR)(ULONG_PTR)0, ERROR_PROC_NOT_FOUND);
    NoFallback(NTDLL_MODULE, (PCSTR)(ULONG_PTR)1, ERROR_PROC_NOT_FOUND);
    NoFallback(NTDLL_MODULE, (PCSTR)(ULONG_PTR)0xffff, ERROR_PROC_NOT_FOUND);
    NoFallback(NTDLL_MODULE, "Unknown", ERROR_PROC_NOT_FOUND);
    NoFallback(NTDLL_MODULE, "rtlGetDeviceFamilyInfoEnum", ERROR_PROC_NOT_FOUND);
    NoFallback(NTDLL_MODULE, "RtlGetDeviceFamilyInfoEnumSuffix", ERROR_PROC_NOT_FOUND);
    NoFallback(NTDLL_MODULE, "RtlGetDeviceFamilyInfoEnu", ERROR_PROC_NOT_FOUND);
    NoFallback(NTDLL_MODULE, target, 6);

    /* Same name on another module is not permission to return a foreign export. */
    Reset(OTHER_MODULE, target);
    assert(Stub_GetProcAddress(OTHER_MODULE, target) == NULL);
    assert(NativeCalls == 1 && NtdllQueries == 0 && ProviderQueries == 0);
    assert(LastError == ERROR_PROC_NOT_FOUND);

    /* The redirected KxNt handle must not be mistaken for canonical NTDLL. */
    Reset(REDIRECTED_NTDLL_MODULE, target);
    RedirectNtdll = 1;
    assert(Stub_GetProcAddress(REDIRECTED_NTDLL_MODULE, target) == NULL);
    assert(NativeCalls == 1 && NtdllQueries == 0 && ProviderQueries == 0);
    assert(LastError == ERROR_PROC_NOT_FOUND);

    Reset(NTDLL_MODULE, target);
    ProcessData.SystemDllBase = NULL;
    assert(Stub_GetProcAddress(NTDLL_MODULE, target) == NULL);
    assert(NativeCalls == 1 && NtdllQueries == 0 && ProviderQueries == 0);
    assert(LastError == ERROR_PROC_NOT_FOUND);

    Reset(NTDLL_MODULE, target);
    ProviderPresent = 0;
    assert(Stub_GetProcAddress(NTDLL_MODULE, target) == NULL);
    assert(NativeCalls == 1 && NtdllQueries == 0 && ProviderQueries == 1);
    assert(LastError == ERROR_PROC_NOT_FOUND);

    Reset(NTDLL_MODULE, target);
    ProviderResult = NULL;
    assert(Stub_GetProcAddress(NTDLL_MODULE, target) == NULL);
    assert(NativeCalls == 2 && NtdllQueries == 0 && ProviderQueries == 1);
    assert(LastError == ERROR_PROC_NOT_FOUND);
    puts("GetProcAddress native-first, restricted fallback and error-preservation tests passed.");
    return 0;
}
