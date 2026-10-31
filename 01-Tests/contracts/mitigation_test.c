#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(_MSC_VER)
/* Windows SDK policy declarations intentionally use anonymous unions. */
#pragma warning(disable:4201)
#endif

typedef int BOOL;
typedef unsigned char BOOLEAN;
typedef unsigned int ULONG;
typedef unsigned int DWORD;
#if defined(_MSC_VER)
typedef unsigned __int64 ULONGLONG;
#else
typedef unsigned long long ULONGLONG;
#endif
typedef ULONGLONG *PULONGLONG;
typedef void *HANDLE;
typedef void *PVOID;
typedef size_t SIZE_T;

#define IN
#define OUT
#define WINAPI
#define KXBASEAPI
#define TRUE 1
#define FALSE 0
#define DUMMYSTRUCTNAME
#define DUMMYUNIONNAME
#define TYPEDEF_TYPE_NAME(Name) Name, *P##Name
#define STATUS_INVALID_PARAMETER ((int)0xc000000d)
#define STATUS_INVALID_BUFFER_SIZE ((int)0xc0000206)
#define STATUS_NOT_SUPPORTED ((int)0xc00000bb)
#define PROCESS_CREATION_MITIGATION_POLICY_VALID_MASK 7
#define NtCurrentProcess() ((HANDLE)(size_t)-1)
#define KexLogWarningEvent(...) ((void)0)

/* The actual policy declarations and function are inserted by run-tests.py. */
/* POLICY_DECLARATIONS */

static int LastError;
static int NativeCalls;
static int FailNative;
static HANDLE ExpectedHandle;
static HANDLE SeenHandle;

static void BaseSetLastNTError(int status)
{
    LastError = status;
}

static BOOL GetProcessDEPPolicy(HANDLE process, ULONG *flags, BOOL *permanent)
{
    ++NativeCalls;
    SeenHandle = process;
    if (FailNative || process != ExpectedHandle) {
        LastError = 6;
        return FALSE;
    }
    *flags = 3;
    *permanent = TRUE;
    return TRUE;
}

/* MITIGATION_FUNCTION */

int main(void)
{
    PROCESS_MITIGATION_DEP_POLICY dep;
    PROCESS_MITIGATION_DEP_POLICY before;
    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY syscalls;
    ULONGLONG mask[2];
    int calls;

    ExpectedHandle = (HANDLE)(size_t)0x1234;
    memset(&dep, 0, sizeof(dep));
    assert(GetProcessMitigationPolicy(ExpectedHandle, ProcessDEPPolicy, &dep, sizeof(dep)));
    assert(SeenHandle == ExpectedHandle);
    assert(dep.Flags.AsUlong == 3);
    assert(dep.Permanent == TRUE);

    /* A failed native query must preserve its error and not copy uninitialized data. */
    memset(&dep, 0xa5, sizeof(dep));
    before = dep;
    FailNative = 1;
    assert(!GetProcessMitigationPolicy(ExpectedHandle, ProcessDEPPolicy, &dep, sizeof(dep)));
    assert(LastError == 6);
    assert(memcmp(&before, &dep, sizeof(dep)) == 0);

    /* An invalid caller-supplied process handle must reach the native query. */
    FailNative = 0;
    assert(!GetProcessMitigationPolicy((HANDLE)0, ProcessDEPPolicy, &dep, sizeof(dep)));
    assert(SeenHandle == (HANDLE)0);
    assert(LastError == 6);

    calls = NativeCalls;
    assert(!GetProcessMitigationPolicy(ExpectedHandle, ProcessDEPPolicy, &dep, sizeof(dep) - 1));
    assert(NativeCalls == calls);
    assert(LastError == STATUS_INVALID_BUFFER_SIZE);

    memset(&syscalls, 0xff, sizeof(syscalls));
    assert(GetProcessMitigationPolicy(ExpectedHandle, ProcessSystemCallDisablePolicy, &syscalls, sizeof(syscalls)));
    assert(syscalls.Flags == 0);

    mask[0] = mask[1] = ~(ULONGLONG)0;
    assert(GetProcessMitigationPolicy(ExpectedHandle, ProcessMitigationOptionsMask, mask, sizeof(mask)));
    assert(mask[0] == PROCESS_CREATION_MITIGATION_POLICY_VALID_MASK);
    assert(mask[1] == 0);
    assert(!GetProcessMitigationPolicy(ExpectedHandle, (PROCESS_MITIGATION_POLICY)99, mask, sizeof(mask)));
    assert(LastError == STATUS_NOT_SUPPORTED);
    puts("Process mitigation API contract tests passed.");
    return 0;
}
