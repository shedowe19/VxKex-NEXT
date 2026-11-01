#include "test_platform.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* The target and neighboring entry are data only; no stub bytes are executed. */
static struct {
    BYTE Before[32];
    BYTE Target[128];
    BYTE After[32];
} Code;
static struct { ULONG Flags; } ProcessData;
#define KexData (&ProcessData)
#define NtOpenKey (Code.Target)

static ULONG CurrentBitness, OperatingSystemBitness, OriginalBuildNumber;
static ULONG SSN_NtOpenKey;
static PVOID ExpectedHookDestination;
static ULONG ProtectCalls, FreeCalls, CopyCalls;
static SIZE_T LastCopySize;

static ULONG KexRtlCurrentProcessBitness(void) { return CurrentBitness; }
static ULONG KexRtlOperatingSystemBitness(void) { return OperatingSystemBitness; }
static HANDLE NtCurrentProcess(void) { return (HANDLE)(ULONG_PTR)-1; }

static void LogEvent(const wchar_t *format, ...)
{
    (void)format;
}

#define KexLogDebugEvent LogEvent
#define KexLogWarningEvent LogEvent
#define KexRtlNtStatusToString(status) "test-status"

static NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE process, PPVOID address,
                                         PSIZE_T size, ULONG free_type)
{
    assert(process == NtCurrentProcess());
    assert(*address == ExpectedHookDestination);
    assert(*size == 4096 && free_type == MEM_RELEASE);
    ++FreeCalls;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE process, PPVOID address,
                                            PSIZE_T size, ULONG protection,
                                            PULONG old_protection)
{
    assert(process == NtCurrentProcess());
    assert(*address == (PVOID)NtOpenKey && *size > 0);
    if (ProtectCalls == 0) {
        assert(protection == PAGE_EXECUTE_WRITECOPY);
        *old_protection = 0x20;
    } else {
        assert(ProtectCalls == 1 && protection == 0x20);
        *old_protection = PAGE_EXECUTE_WRITECOPY;
    }
    ++ProtectCalls;
    return STATUS_SUCCESS;
}

static void CopyMemory(PVOID destination, const void *source, SIZE_T length)
{
    assert(destination == (PVOID)NtOpenKey);
    assert(length <= sizeof(Code.Target));
    ++CopyCalls;
    LastCopySize = length;
    memcpy(destination, source, length);
}

#define RtlCopyMemory CopyMemory

/* All four runtime branches are compiled on each host. The original routine
   intentionally converts 32/64-bit encoded hook addresses to native pointers. */
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4302 4311 4312)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#endif
/* PRODUCTION_FUNCTION */
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

static const BYTE Native64[] = {
    0x4c, 0x8b, 0xd1, 0xb8, 0x0f, 0x00, 0x00, 0x00,
    0x0f, 0x05, 0xc3
};
static const BYTE Wow64Legacy[] = {
    0xb8, 0x0f, 0x00, 0x00, 0x00, 0x31, 0xc9,
    0x8d, 0x54, 0x24, 0x04,
    0x64, 0xff, 0x15, 0xc0, 0x00, 0x00, 0x00,
    0x83, 0xc4, 0x04, 0xc2, 0x0c, 0x00
};
static const BYTE Wow64Modern[] = {
    0xb8, 0x0f, 0x00, 0x00, 0x00,
    0x64, 0xff, 0x15, 0xc0, 0x00, 0x00, 0x00, 0xc2, 0x0c, 0x00
};
static const BYTE Native32[] = {
    0xb8, 0xb6, 0x00, 0x00, 0x00,
    0x8d, 0x54, 0x24, 0x04, 0xcd, 0x2e, 0xc2, 0x0c, 0x00
};

static void SetUp(ULONG current, ULONG operating_system, ULONG build)
{
    ULONG index;
    CurrentBitness = current;
    OperatingSystemBitness = operating_system;
    OriginalBuildNumber = build;
    ProcessData.Flags = KEXDATA_FLAG_PROPAGATED;
    ProtectCalls = FreeCalls = CopyCalls = 0;
    LastCopySize = 0;
    ExpectedHookDestination = (PVOID)(ULONG_PTR)0x12345678U;
    memset(&Code, 0xa5, sizeof(Code));
    /* A neighboring syscall entry and trailing canaries must survive cleanup. */
    for (index = 0; index < sizeof(Code.Target); ++index) {
        Code.Target[index] = (BYTE)(0x80U + index);
    }
}

static void InstallSyntheticHook(void)
{
    if (CurrentBitness == 64) {
        ULONGLONG destination;
        destination = (ULONGLONG)(ULONG_PTR)ExpectedHookDestination;
        Code.Target[0] = 0xff;
        Code.Target[1] = 0x25;
        memset(Code.Target + 2, 0, 4);
        memcpy(Code.Target + 6, &destination, sizeof(destination));
    } else {
        ULONG destination;
        destination = (ULONG)(ULONG_PTR)ExpectedHookDestination;
        Code.Target[0] = 0x68;
        memcpy(Code.Target + 1, &destination, sizeof(destination));
        Code.Target[5] = 0xc3;
    }
}

static int CheckRestoration(ULONG current, ULONG operating_system, ULONG build,
                            const BYTE *expected_template, SIZE_T stub_size,
                            SIZE_T next_entry, ULONG service_id)
{
    BYTE expected[sizeof(Code.Target)];
    BYTE before[sizeof(Code.Before)], after[sizeof(Code.After)];
    SIZE_T service_offset;
    SetUp(current, operating_system, build);
    SSN_NtOpenKey = service_id;
    memcpy(expected, Code.Target, sizeof(expected));
    memcpy(expected, expected_template, stub_size);
    service_offset = current == 64 ? 4 : 1;
    expected[service_offset] = (BYTE)service_id;
    /* The next syscall begins with MOV EAX, imm32, not RET 12. */
    expected[next_entry] = 0xb8;
    expected[next_entry + 1] = (BYTE)(service_id + 1);
    expected[next_entry + 2] = expected[next_entry + 3] = expected[next_entry + 4] = 0;
    memcpy(Code.Target, expected, sizeof(expected));
    memcpy(before, Code.Before, sizeof(before));
    memcpy(after, Code.After, sizeof(after));
    InstallSyntheticHook();

    KexpCleanupPropagationRemains();

    if (memcmp(Code.Target + next_entry, expected + next_entry,
               sizeof(expected) - next_entry) != 0) {
        fprintf(stderr, "Adjacent syscall corrupted: current=%u OS=%u build=%u, "
                "restored=%lu bytes, next entry starts at %lu\n",
                current, operating_system, build, (unsigned long)LastCopySize,
                (unsigned long)next_entry);
        return 0;
    }
    assert(FreeCalls == 1 && ProtectCalls == 2 && CopyCalls == 1);
    assert(LastCopySize == stub_size);
    /* XOR ECX,ECX has two equivalent encodings; both retain the 24-byte ABI. */
    if (current == 32 && operating_system == 64 && build < 8102 &&
        Code.Target[5] == 0x33 && Code.Target[6] == 0xc9) {
        expected[5] = 0x33;
    }
    assert(memcmp(Code.Target, expected, stub_size) == 0);
    assert(Code.Target[service_offset] == (BYTE)service_id);
    if (current == 64) {
        assert(Code.Target[stub_size - 1] == 0xc3);
    } else {
        assert(Code.Target[stub_size - 3] == 0xc2);
        assert(Code.Target[stub_size - 2] == 0x0c && Code.Target[stub_size - 1] == 0);
    }
    assert(memcmp(Code.Before, before, sizeof(before)) == 0);
    assert(memcmp(Code.After, after, sizeof(after)) == 0);
    return 1;
}

static void CheckNoOp(int propagated, int hook_present)
{
    BYTE before[sizeof(Code)];
    SetUp(32, 64, 7601);
    if (!propagated) ProcessData.Flags = 0;
    if (hook_present) InstallSyntheticHook();
    memcpy(before, &Code, sizeof(before));
    KexpCleanupPropagationRemains();
    assert(memcmp(before, &Code, sizeof(before)) == 0);
    assert(ProtectCalls == 0 && FreeCalls == 0 && CopyCalls == 0);
}

int main(void)
{
    ULONG slot;
    int passed;
    assert(sizeof(ULONG) == 4 && sizeof(ULONGLONG) == 8);
    assert(sizeof(ULONG_PTR) == sizeof(void *));
    passed = 1;
    for (slot = 0; slot != 2; ++slot) {
        passed &= CheckRestoration(64, 64, 7601, Native64, sizeof(Native64), 16, 0x0fU + slot);
        passed &= CheckRestoration(32, 64, 7601, Wow64Legacy, sizeof(Wow64Legacy), 24, 0x0fU + slot);
        passed &= CheckRestoration(32, 64, 9200, Wow64Modern, sizeof(Wow64Modern), 15, 0x0fU + slot);
        passed &= CheckRestoration(32, 32, 7601, Native32, sizeof(Native32), 14, 0xb6U + slot);
    }
    CheckNoOp(0, 1);
    CheckNoOp(0, 0);
    CheckNoOp(1, 0);
    if (!passed) return 1;
    puts("Propagation cleanup ABI/adjacent-syscall/canary tests passed for all four branches.");
    return 0;
}
