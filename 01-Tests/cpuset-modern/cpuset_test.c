/* Host contract test for the production CPU set implementation.
 * Compiles the real cpuset.c with the Win7 SDK, runs on the build host and
 * compares topology/size semantics with the native APIs when present. It only
 * changes its own process/thread affinity and restores it. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define STATIC static
#define KXBASEAPI
typedef LONG NTSTATUS;
static void BaseSetLastNTError(NTSTATUS Status) { SetLastError((DWORD) Status); }

/* CPU_SET_BODY */

typedef BOOL (WINAPI *PFN_GETSYSTEM)(PVOID, ULONG, PULONG, HANDLE, ULONG);
typedef struct _TEST_CPU_SET {
    DWORD Size;
    DWORD Type;
    struct {
        DWORD Id;
        WORD Group;
        BYTE Lpi, Core, Llc, Numa, Eff, Flags;
        DWORD Reserved;
        DWORD64 AllocationTag;
    } CpuSet;
} TEST_CPU_SET;

static ULONG Checks, Failures;

static void Check(BOOL Condition, const char *Name)
{
    ++Checks;
    if (!Condition) { ++Failures; printf("FAIL|%s\n", Name); }
}

int main(void)
{
    ULONG Required = 0, Count, Index, NativeRequired = 0, NativeCount = 0;
    PKX_SYSTEM_CPU_SET_INFORMATION Info = NULL, NativeInfo = NULL;
    PFN_GETSYSTEM Native = (PFN_GETSYSTEM) GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemCpuSetInformation");
    KAFFINITY OriginalProcess, SystemMask, Observed;
    ULONG DefaultCount = 0, ThreadCount = 0;
    PULONG Ids = NULL;
    BOOL Ok;
    HANDLE Thread = GetCurrentThread();
    DWORD Saved;
    ULONG FirstId = 0;
    KAFFINITY FirstBit = 0;

    /* 1. Size query. */
    SetLastError(0xdeadbeef);
    Ok = GetSystemCpuSetInformation(NULL, 0, &Required, NULL, 0);
    Check(!Ok && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "size query fails with ERROR_INSUFFICIENT_BUFFER");
    Check(Required != 0 && Required % sizeof(KX_SYSTEM_CPU_SET_INFORMATION) == 0,
        "required size is a whole number of records");
    Check(sizeof(KX_SYSTEM_CPU_SET_INFORMATION) == 32, "record size is 32 bytes");

    /* 2. Enumerate our topology. */
    Info = (PKX_SYSTEM_CPU_SET_INFORMATION) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required);
    Count = Required / sizeof(KX_SYSTEM_CPU_SET_INFORMATION);
    Ok = GetSystemCpuSetInformation(Info, Required, &Required, NULL, 0);
    Check(Ok && Required == Count * sizeof(KX_SYSTEM_CPU_SET_INFORMATION),
        "enumeration returns the full record array");
    for (Index = 0; Index < Count; ++Index) {
        Check(Info[Index].Size == 32 && Info[Index].Type == 0, "record size/type");
        Check(Info[Index].CpuSet.Id ==
            (((ULONG) Info[Index].CpuSet.Group << 16) | Info[Index].CpuSet.LogicalProcessorIndex),
            "stable ID encoding");
        Check((Info[Index].CpuSet.AllFlags & 0x2) != 0, "allocated flag set");
        if (Index && Info[Index].CpuSet.Id <= Info[Index - 1].CpuSet.Id) {
            Check(FALSE, "IDs strictly increasing");
        }
    }

    /* 3. Compare count and group/LP pairs with the native API. */
    if (Native) {
        NativeRequired = 0;
        Ok = Native(NULL, 0, &NativeRequired, NULL, 0);
        Check(!Ok && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "native size query semantics");
        NativeInfo = (PKX_SYSTEM_CPU_SET_INFORMATION) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NativeRequired);
        Ok = Native(NativeInfo, NativeRequired, &NativeRequired, NULL, 0);
        NativeCount = NativeRequired / 32;
        Check(Ok && NativeCount == Count, "same logical processor count as native");
        if (NativeCount == Count) {
            for (Index = 0; Index < Count; ++Index) {
                Check(NativeInfo[Index].CpuSet.Group == Info[Index].CpuSet.Group &&
                    NativeInfo[Index].CpuSet.LogicalProcessorIndex == Info[Index].CpuSet.LogicalProcessorIndex,
                    "same group/logical processor pair as native");
            }
        }
    }

    /* 4. Initial default set is empty and LastError is preserved. */
    Saved = 0xdeadbeef; SetLastError(Saved);
    Ok = GetProcessDefaultCpuSets(NULL, NULL, 0, &DefaultCount);
    Check(Ok && DefaultCount == 0 && GetLastError() == Saved,
        "initial process default set is empty and preserves LastError");

    Check(GetProcessAffinityMask(GetCurrentProcess(), &OriginalProcess, &SystemMask),
        "initial affinity query");

    /* 5. Apply one CPU set and verify the real affinity restriction. */
    for (Index = 0; Index < Count; ++Index) {
        if (OriginalProcess & ((KAFFINITY) 1 << Info[Index].CpuSet.LogicalProcessorIndex)) {
            FirstId = Info[Index].CpuSet.Id;
            FirstBit = (KAFFINITY) 1 << Info[Index].CpuSet.LogicalProcessorIndex;
            break;
        }
    }
    Check(FirstBit != 0, "found an allowed CPU set");

    SetLastError(Saved);
    Ok = SetProcessDefaultCpuSets(NULL, &FirstId, 1);
    Check(Ok && GetLastError() == Saved, "set process default set succeeds and preserves LastError");
    Check(GetProcessAffinityMask(GetCurrentProcess(), &Observed, &SystemMask) && Observed == FirstBit,
        "process affinity is the requested set");
    DefaultCount = 0;
    Ok = GetProcessDefaultCpuSets(NULL, NULL, 0, &DefaultCount);
    if (!Ok || DefaultCount != 1) printf("INFO|default-count|ok=%lu|count=%lu|err=%lu\n", Ok ? 1UL : 0UL, DefaultCount, GetLastError());
    Check(Ok && DefaultCount == 1, "default set count is one");
    Ids = (PULONG) HeapAlloc(GetProcessHeap(), 0, sizeof(ULONG));
    Ok = GetProcessDefaultCpuSets(NULL, Ids, 1, &DefaultCount);
    Check(Ok && DefaultCount == 1 && Ids[0] == FirstId, "default set roundtrip");

    /* 6. Clear and verify restoration. */
    Ok = SetProcessDefaultCpuSets(NULL, NULL, 0);
    Check(Ok, "clear default set");
    Check(GetProcessAffinityMask(GetCurrentProcess(), &Observed, &SystemMask) && Observed == OriginalProcess,
        "original affinity restored");
    DefaultCount = 99;
    Ok = GetProcessDefaultCpuSets(NULL, NULL, 0, &DefaultCount);
    Check(Ok && DefaultCount == 0, "default set empty after clear");

    /* 7. Thread selected set roundtrip. */
    ThreadCount = 99;
    Ok = GetThreadSelectedCpuSets(Thread, NULL, 0, &ThreadCount);
    Check(Ok && ThreadCount == 0, "initial thread selection is empty");
    SetLastError(Saved);
    Ok = SetThreadSelectedCpuSets(Thread, &FirstId, 1);
    if (!Ok) printf("INFO|thread-set|err=%lu\n", GetLastError());
    Check(Ok && GetLastError() == Saved, "set thread selection succeeds");
    {
        GROUP_AFFINITY Group;
        ZeroMemory(&Group, sizeof(Group));
        Check(GetThreadGroupAffinity(Thread, &Group) && Group.Mask == FirstBit,
            "thread affinity is the selected set");
    }
    ThreadCount = 0;
    Ok = GetThreadSelectedCpuSets(Thread, NULL, 0, &ThreadCount);
    Check(Ok && ThreadCount == 1, "thread selection count is one");
    Ok = SetThreadSelectedCpuSets(Thread, NULL, 0);
    Check(Ok, "clear thread selection");
    {
        GROUP_AFFINITY Group;
        ZeroMemory(&Group, sizeof(Group));
        Check(GetThreadGroupAffinity(Thread, &Group) && Group.Mask == OriginalProcess,
            "thread affinity restored");
    }

    /* 8. Error contract. */
    Required = 0;
    Ok = GetSystemCpuSetInformation(NULL, 0, &Required, NULL, 1);
    Check(!Ok && GetLastError() == ERROR_INVALID_PARAMETER, "reserved flags are rejected");
    DefaultCount = 0;
    Ok = GetProcessDefaultCpuSets((HANDLE) (ULONG_PTR) 0x1234, NULL, 0, &DefaultCount);
    Check(!Ok && GetLastError() == ERROR_INVALID_HANDLE, "invalid process handle is rejected");
    {
        ULONG Invalid = 0x7fffffff;
        Ok = SetProcessDefaultCpuSets(NULL, &Invalid, 1);
        Check(!Ok && GetLastError() == ERROR_INVALID_PARAMETER, "unknown CPU set ID is rejected");
    }

    printf("CPUSET_SUMMARY|checks=%lu|failures=%lu|processors=%lu|native=%lu\n",
        Checks, Failures, Count, NativeCount);
    return Failures ? 1 : 0;
}
