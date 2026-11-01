/* Compile the actual cleanup; never execute a real syscall or modify an OS DLL. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning(disable: 4305 4306 4311 4312) /* Exercise both target bitness branches. */
#endif
#define STATIC static
#define VOID void
#define ASSERT(expression) ((void)0) /* Release behavior: exercise explicit guards. */
#define NT_SUCCESS(s) ((s) >= 0)
#define KEXDATA_FLAG_PROPAGATED 1
#define PAGE_EXECUTE_WRITECOPY 0x80
#define MEM_RELEASE 0x8000
#define LOWORD(n) ((unsigned short)(n))
#define unless(x) if (!(x))
#define KexLogDebugEvent(...) ((void)0)
#define KexLogWarningEvent(...) ((void)0)
typedef int32_t NTSTATUS;
typedef uint32_t ULONG;
typedef ULONG *PULONG;
typedef uint64_t ULONGLONG;
typedef ULONGLONG *PULONGLONG;
typedef unsigned char BYTE;
typedef BYTE *PBYTE;
typedef void *PVOID;
typedef size_t SIZE_T;
typedef struct { ULONG Flags; } TEST_DATA;

static TEST_DATA Data;
static TEST_DATA *KexData = &Data;
static ULONG OriginalBuildNumber, SSN_NtOpenKey = 0x37;
static unsigned ProcessBits, OsBits;
static BYTE Arena[16384], Before[96];
static PBYTE NtOpenKey;
static PVOID ExpectedHook;
static SIZE_T ExpectedBytes;
static int Writable, ProtectCalls, CopyCalls, FlushCalls, FreeCalls;
static int FailProtect, FailFlush, FailRestore, FailFree;
static int Events[8], EventCount;

static void Event(int value) { assert(EventCount < 8); Events[EventCount++] = value; }
static unsigned KexRtlCurrentProcessBitness(void) { return ProcessBits; }
static unsigned KexRtlOperatingSystemBitness(void) { return OsBits; }
#define NtCurrentProcess() ((PVOID)(uintptr_t)1)

static NTSTATUS NtProtectVirtualMemory(PVOID process, PVOID *base, SIZE_T *size, ULONG protect, PULONG old) {
    assert(process == NtCurrentProcess());
    ++ProtectCalls;
    if (ProtectCalls == 1) {
        Event(1);
        assert(*base == NtOpenKey);
        assert(*size == ExpectedBytes);
        assert(protect == PAGE_EXECUTE_WRITECOPY);
        if (FailProtect) return -1;
        *old = 0x20;
        *base = (PVOID)((uintptr_t)NtOpenKey & ~(uintptr_t)4095);
        *size = 8192; /* Round across the synthetic page boundary. */
        Writable = 1;
    } else {
        Event(4);
        assert(ProtectCalls == 2 && Writable && protect == 0x20);
        assert(*base == (PVOID)((uintptr_t)NtOpenKey & ~(uintptr_t)4095));
        assert(*size == 8192);
        if (FailRestore) return -2;
        Writable = 0;
        *old = PAGE_EXECUTE_WRITECOPY;
    }
    return 0;
}
static void RtlCopyMemory(PVOID target, const void *source, SIZE_T bytes) {
    Event(2);
    assert(target == NtOpenKey && Writable);
    assert(bytes == ExpectedBytes);
    assert(bytes <= sizeof(Before));
    ++CopyCalls;
    memcpy(target, source, bytes);
}
static NTSTATUS NtFlushInstructionCache(PVOID process, PVOID base, SIZE_T bytes) {
    Event(3);
    assert(process == NtCurrentProcess() && base == NtOpenKey && bytes == ExpectedBytes);
    assert(CopyCalls == 1);
    ++FlushCalls;
    return FailFlush ? -3 : 0;
}
static NTSTATUS NtFreeVirtualMemory(PVOID process, PVOID *base, SIZE_T *size, ULONG type) {
    Event(5);
    assert(process == NtCurrentProcess() && *base == ExpectedHook && type == MEM_RELEASE);
    {
        assert(*size == 0);
        assert(CopyCalls == 1 && FlushCalls == 1 && ProtectCalls == 2 && !Writable);
        assert(!FailFlush && !FailRestore);
    }
    ++FreeCalls;
    return FailFree ? -4 : 0;
}

/* PRODUCTION_FUNCTION */

static void Reset(unsigned process, unsigned os, ULONG build, SIZE_T bytes) {
    uintptr_t page = ((uintptr_t)Arena + 4095) & ~(uintptr_t)4095;
    ProcessBits = process; OsBits = os; OriginalBuildNumber = build;
    NtOpenKey = (PBYTE)(page + 4096 - 20); /* Legacy 24-byte write spans pages. */
    ExpectedHook = (PVOID)(uintptr_t)0x12345678;
    ExpectedBytes = bytes;
    Data.Flags = KEXDATA_FLAG_PROPAGATED;
    Writable = ProtectCalls = CopyCalls = FlushCalls = FreeCalls = EventCount = 0;
    FailProtect = FailFlush = FailRestore = FailFree = 0;
    memset(NtOpenKey, 0xA5, sizeof(Before));
    if (process == 64) {
        ULONGLONG hook = (ULONGLONG)(uintptr_t)ExpectedHook;
        NtOpenKey[0] = 0xFF; NtOpenKey[1] = 0x25;
        memset(NtOpenKey + 2, 0, 4);
        memcpy(NtOpenKey + 6, &hook, sizeof(hook));
    } else {
        ULONG hook = (ULONG)(uintptr_t)ExpectedHook;
        NtOpenKey[0] = 0x68; NtOpenKey[5] = 0xC3;
        memcpy(NtOpenKey + 1, &hook, sizeof(hook));
    }
    memcpy(Before, NtOpenKey, sizeof(Before));
}
static int NeighborIntact(void) {
    return memcmp(NtOpenKey + ExpectedBytes, Before + ExpectedBytes, sizeof(Before) - ExpectedBytes) == 0;
}
static void Successful(unsigned process, unsigned os, ULONG build, const BYTE *expected, SIZE_T bytes) {
    static const int order[] = { 1, 2, 3, 4, 5 };
    Reset(process, os, build, bytes);
    KexpCleanupPropagationRemains();
    assert(memcmp(NtOpenKey, expected, bytes) == 0);
    assert(NeighborIntact());
    assert(ProtectCalls == 2 && CopyCalls == 1 && FlushCalls == 1 && FreeCalls == 1);
    assert(EventCount == 5 && memcmp(Events, order, sizeof(order)) == 0);
}
int main(void) {
    static const BYTE legacy[] = {0xB8,0x37,0,0,0,0x31,0xC9,0x8D,0x54,0x24,4,0x64,0xFF,0x15,0xC0,0,0,0,0x83,0xC4,4,0xC2,0x0C,0};
    static const BYTE modern[] = {0xB8,0x37,0,0,0,0x64,0xFF,0x15,0xC0,0,0,0,0xC2,0x0C,0};
    static const BYTE native32[] = {0xB8,0x37,0,0,0,0x8D,0x54,0x24,4,0xCD,0x2E,0xC2,0x0C,0};
    static const BYTE native64[] = {0x4C,0x8B,0xD1,0xB8,0x37,0,0,0,0x0F,0x05,0xC3};
    Successful(32,64,7601,legacy,sizeof(legacy));
    Successful(32,64,9200,modern,sizeof(modern));
    Successful(32,32,7601,native32,sizeof(native32));
    Successful(64,64,7601,native64,sizeof(native64));
    Reset(32,64,7601,24); Data.Flags=0; KexpCleanupPropagationRemains();
    assert(!ProtectCalls && !CopyCalls && !FreeCalls);
    Reset(32,64,7601,24); NtOpenKey[0]=0xB8; KexpCleanupPropagationRemains();
    assert(!ProtectCalls && !CopyCalls && !FreeCalls);
    Reset(32,64,7601,24); FailProtect=1; KexpCleanupPropagationRemains();
    assert(ProtectCalls==1 && !CopyCalls && !FlushCalls && !FreeCalls);
    assert(memcmp(NtOpenKey,Before,sizeof(Before))==0);
    Reset(32,64,7601,24); FailFlush=1; KexpCleanupPropagationRemains();
    assert(CopyCalls==1 && FlushCalls==1 && ProtectCalls==2 && !Writable && !FreeCalls && NeighborIntact());
    Reset(32,64,7601,24); FailRestore=1; KexpCleanupPropagationRemains();
    assert(CopyCalls==1 && FlushCalls==1 && ProtectCalls==2 && Writable && !FreeCalls && NeighborIntact());
    Reset(32,64,7601,24); FailFree=1; KexpCleanupPropagationRemains();
    assert(FreeCalls==1 && !Writable && NeighborIntact() && memcmp(NtOpenKey,legacy,sizeof(legacy))==0);
    puts("Propagation cleanup: adjacent syscall canary, all template routes and failure ordering passed.");
    return 0;
}
