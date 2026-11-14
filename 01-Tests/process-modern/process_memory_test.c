/* Exercise the production entry points with controlled native failures and
 * actual Windows pointer protections. This executable changes no OS policy. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define KXBASEAPI
#define try __try
#define except __except
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#define STATUS_SUCCESS ((LONG)0)
#define STATUS_ACCESS_DENIED ((LONG)0xc0000022)
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((LONG)0xc0000008)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((LONG)0xc000000d)
#endif
#define STATUS_NOT_SUPPORTED ((LONG)0xc00000bb)
#define STATUS_INFO_LENGTH_MISMATCH ((LONG)0xc0000004)
typedef LONG NTSTATUS;
typedef struct _PAGE_PRIORITY_INFORMATION { ULONG PagePriority; } PAGE_PRIORITY_INFORMATION, *PPAGE_PRIORITY_INFORMATION;
/* PROCESS_INFORMATION_CLASS */
enum { ProcessPagePriority = 39 };

typedef ULONG (WINAPI *STATUS_ERROR)(LONG);
static STATUS_ERROR Translate;
static ULONG Assertions, Failures, Queries, Sets, KernelPriority;
static LONG QueryStatus, SetStatus;
static HANDLE ExpectedHandle;
static PVOID CallerBuffer;
static const DWORD Sentinel = 0x66554433;

static void Check(BOOL Condition, const char *Name)
{
    ++Assertions;
    if (!Condition) { ++Failures; printf("FAIL: %s\n", Name); }
}

static void BaseSetLastNTError(NTSTATUS Status)
{
    SetLastError(Translate(Status));
}

static NTSTATUS NtQueryInformationProcess(HANDLE Handle, ULONG Class,
    PVOID Buffer, ULONG Bytes, PULONG Returned)
{
    ++Queries;
    Check(Handle == ExpectedHandle, "query retains supplied handle");
    Check(Class == ProcessPagePriority && Bytes == sizeof(ULONG), "query uses real class39 ABI");
    Check(Returned == NULL, "query accepts native contract without ReturnLength");
    Check(Buffer != CallerBuffer, "query publishes through local buffer");
    *(PULONG)Buffer = KernelPriority;
    SetLastError(0xdeadbeef);
    return QueryStatus;
}

static NTSTATUS NtSetInformationProcess(HANDLE Handle, ULONG Class,
    PVOID Buffer, ULONG Bytes)
{
    ++Sets;
    Check(Handle == ExpectedHandle, "setter retains supplied handle");
    Check(Class == ProcessPagePriority && Bytes == sizeof(ULONG), "setter uses real class39 ABI");
    Check(Buffer != CallerBuffer, "setter snapshots caller once");
    if (NT_SUCCESS(SetStatus)) KernelPriority = *(PULONG)Buffer;
    SetLastError(0xdeadbeef);
    return SetStatus;
}

/* PROCESS_INFORMATION_FUNCTIONS */

static void Reset(PVOID Buffer)
{
    Queries = Sets = 0;
    QueryStatus = SetStatus = STATUS_SUCCESS;
    ExpectedHandle = (HANDLE)(ULONG_PTR)0x1234;
    CallerBuffer = Buffer;
    SetLastError(Sentinel);
}

int main(void)
{
    union { FARPROC Raw; STATUS_ERROR Typed; } Function;
    struct { ULONG Before, Value, After; } Output;
    ULONG Value, Index, Bytes, Before;
    ULONG InvalidSizes[] = {0, 3, 8};
    ULONG InvalidValues[] = {0, 6, 0xffffffff};
    LONG NativeFailures[] = {STATUS_ACCESS_DENIED, STATUS_INVALID_HANDLE, STATUS_NOT_SUPPORTED};
    BYTE *Page;
    DWORD OldProtection;
    Function.Raw = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError");
    Translate = Function.Typed;
    if (!Translate) return 2;

    Output.Before = 0xaabbccdd; Output.After = 0x11223344;
    for (Value = 1; Value <= 5; ++Value) {
        KernelPriority = Value;
        Output.Value = 0xa5a5a5a5;
        Reset(&Output.Value);
        Check(GetProcessInformation(ExpectedHandle, ProcessMemoryPriority, &Output.Value, 4), "getter succeeds");
        Check(GetLastError() == Sentinel, "getter preserves LastError");
        Check(Output.Value == Value && Output.Before == 0xaabbccdd && Output.After == 0x11223344,
            "getter copies only actual kernel value");
        Check(Queries == 1 && Sets == 0, "getter performs one real query");

        KernelPriority = 5;
        Reset(&Value);
        Check(SetProcessInformation(ExpectedHandle, ProcessMemoryPriority, &Value, 4), "setter succeeds");
        Check(GetLastError() == Sentinel, "setter preserves LastError");
        Check(KernelPriority == Value && Value >= 1 && Value <= 5, "setter reaches kernel without changing input");
        Check(Queries == 0 && Sets == 1, "setter performs one real set");
    }

    for (Index = 0; Index < sizeof(NativeFailures)/sizeof(NativeFailures[0]); ++Index) {
        Output.Value = 0xa5a5a5a5;
        Reset(&Output.Value); QueryStatus = NativeFailures[Index];
        Check(!GetProcessInformation(ExpectedHandle, ProcessMemoryPriority, &Output.Value, 4), "native query failure propagates");
        Check(GetLastError() == Translate(NativeFailures[Index]), "query maps native error");
        Check(Output.Value == 0xa5a5a5a5, "failed native query never publishes partial data");
        Value = 3; KernelPriority = 5;
        Reset(&Value); SetStatus = NativeFailures[Index];
        Check(!SetProcessInformation(ExpectedHandle, ProcessMemoryPriority, &Value, 4), "native setter failure propagates");
        Check(GetLastError() == Translate(NativeFailures[Index]), "setter maps native error");
        Check(KernelPriority == 5 && Value == 3, "failed native setter changes no state");
    }

    for (Index = 0; Index < sizeof(InvalidSizes)/sizeof(InvalidSizes[0]); ++Index) {
        Bytes = InvalidSizes[Index]; Value = 3;
        Reset(&Value);
        Check(!GetProcessInformation(ExpectedHandle, ProcessMemoryPriority, &Value, Bytes), "get rejects size mismatch");
        Check(GetLastError() == ERROR_BAD_LENGTH && Value == 3 && Queries == 0, "get validates size before native query");
        Reset(&Value);
        Check(!SetProcessInformation(ExpectedHandle, ProcessMemoryPriority, &Value, Bytes), "set rejects size mismatch");
        Check(GetLastError() == ERROR_BAD_LENGTH && Value == 3 && Sets == 0, "set validates size before native call");
    }
    for (Index = 0; Index < sizeof(InvalidValues)/sizeof(InvalidValues[0]); ++Index) {
        Value = InvalidValues[Index]; KernelPriority = 5;
        Reset(&Value);
        Check(!SetProcessInformation(ExpectedHandle, ProcessMemoryPriority, &Value, 4), "set rejects priority outside 1 through 5");
        Check(GetLastError() == ERROR_INVALID_PARAMETER && Sets == 0 && KernelPriority == 5, "invalid priority cannot mutate kernel");
    }

    Value = 3;
    for (Index = 1; Index <= (ULONG)ProcessInformationClassMax; ++Index) {
        DWORD ExpectedError = Index == ProcessInformationClassMax ? ERROR_INVALID_PARAMETER : ERROR_NOT_SUPPORTED;
        Reset(&Value);
        Check(!GetProcessInformation(ExpectedHandle, (PROCESS_INFORMATION_CLASS)Index, &Value, 4), "unsupported get class fails");
        Check(GetLastError() == ExpectedError && Queries == 0, "unsupported get never invents data");
        Reset(&Value);
        Check(!SetProcessInformation(ExpectedHandle, (PROCESS_INFORMATION_CLASS)Index, &Value, 4), "unsupported set class fails");
        Check(GetLastError() == ExpectedError && Sets == 0, "unsupported set never claims an effect");
    }
    Reset(&Value);
    Check(!GetProcessInformation(ExpectedHandle, (PROCESS_INFORMATION_CLASS)-1, &Value, 4) && GetLastError() == ERROR_INVALID_PARAMETER,
        "negative information class is invalid");

    Reset(NULL);
    Check(!GetProcessInformation(ExpectedHandle, ProcessMemoryPriority, NULL, 4) && GetLastError() == ERROR_NOACCESS,
        "NULL query output is caught");
    Reset(NULL);
    Check(!SetProcessInformation(ExpectedHandle, ProcessMemoryPriority, NULL, 4) && GetLastError() == ERROR_NOACCESS && Sets == 0,
        "NULL setter input is caught before mutation");
    Reset((PVOID)(ULONG_PTR)1);
    Check(!SetProcessInformation(ExpectedHandle, ProcessMemoryPriority, (PVOID)(ULONG_PTR)1, 4) && GetLastError() == ERROR_NOACCESS && Sets == 0,
        "inaccessible setter input is caught before mutation");

    Page = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(Page != NULL, "owned page allocation");
    if (Page) {
        *(PULONG)Page = 2;
        Check(VirtualProtect(Page, 4096, PAGE_READONLY, &OldProtection), "owned page read-only");
        Reset(Page); KernelPriority = 5;
        Check(!GetProcessInformation(ExpectedHandle, ProcessMemoryPriority, Page, 4) && GetLastError() == ERROR_NOACCESS,
            "read-only getter output is caught");
        Check(*(PULONG)Page == 2, "failed copy leaves protected page intact");
        Reset(Page);
        Check(SetProcessInformation(ExpectedHandle, ProcessMemoryPriority, Page, 4) && KernelPriority == 2,
            "setter reads but never writes its input");
        Before = Failures;
        Check(VirtualFree(Page, 0, MEM_RELEASE), "owned page released");
        Check(Before == Failures, "owned page cleanup succeeds");
    }
    printf("process-memory contract: %lu assertions, %lu failures\n", Assertions, Failures);
    return Failures ? 1 : 0;
}
