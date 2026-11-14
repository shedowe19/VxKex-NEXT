#include "uac_test_shim.h"
typedef struct { PCSTR Name; PVOID Native,Replacement; } KEX_UAC_ROUTE;
static KEX_UAC_ROUTE KexpUacRoutes[] = {
    {"ShellExecuteA",NULL,NULL},{"ShellExecuteW",NULL,NULL},
    {"ShellExecuteExA",NULL,NULL},{"ShellExecuteExW",NULL,NULL},
    {"LoadLibraryA",NULL,NULL},{"LoadLibraryW",NULL,NULL},
    {"LoadLibraryExA",NULL,NULL},{"LoadLibraryExW",NULL,NULL}
};
static struct TEST_TEB { ULONG LastErrorValue; } TypedTeb;
static ULONG Locks,Unlocks,Resolved;
static int LockFails;
static struct TEST_TEB *MockNtCurrentTeb(void) { return &TypedTeb; }
#define NtCurrentTeb MockNtCurrentTeb
static NTSTATUS NTAPI MockLdrLockLoaderLock(ULONG Flags,PVOID Disposition,PVOID *Cookie)
{ (void)Flags;(void)Disposition;++Locks;TypedTeb.LastErrorValue=777;*Cookie=NULL;return LockFails?(NTSTATUS)-1:0; }
static NTSTATUS NTAPI MockLdrUnlockLoaderLock(ULONG Flags,PVOID Cookie)
{ (void)Flags;(void)Cookie;++Unlocks;TypedTeb.LastErrorValue=778;return 0; }
static void KexpUacResolveNative(void) { ++Resolved;TypedTeb.LastErrorValue=779; }
#define LdrLockLoaderLock MockLdrLockLoaderLock
#define LdrUnlockLoaderLock MockLdrUnlockLoaderLock
/* ACTUAL_FUNCTIONS */
int main(void)
{
    IMAGE_SECTION_HEADER Sections[2];ULONG Index;char LongName[65];
    for(Index=0;Index<ARRAYSIZE(KexpUacRoutes);++Index){KexpUacRoutes[Index].Native=(PVOID)(ULONG_PTR)(0x10000+Index);KexpUacRoutes[Index].Replacement=(PVOID)(ULONG_PTR)(0x20000+Index);}
    CHECK(KexpUacNameEqual("ShellExecuteExW","ShellExecuteExW"));
    CHECK(!KexpUacNameEqual("ShellExecuteExWz","ShellExecuteExW"));CHECK(!KexpUacNameEqual("shellexecuteexw","ShellExecuteExW"));
    memset(LongName,'a',64);LongName[64]=0;CHECK(!KexpUacNameEqual(LongName,LongName));
    CHECK(KexpUacRange(0,8,8));CHECK(KexpUacRange(7,1,8));CHECK(!KexpUacRange(8,0,8));
    CHECK(!KexpUacRange(MAXDWORD,2,MAXDWORD));CHECK(!KexpUacRange(MAXDWORD-2,8,MAXDWORD));
    CHECK(KexpUacRange(MAXDWORD-2,2,MAXDWORD));
    ZeroMemory(Sections,sizeof(Sections));Sections[0].VirtualAddress=0x1000;Sections[0].Misc.VirtualSize=0x100;
    CHECK(KexpUacDataSlot(Sections,1,0x1000,0x2000));
    CHECK(KexpUacDataSlot(Sections,1,0x1100-(ULONG)sizeof(PVOID),0x2000));
    CHECK(!KexpUacDataSlot(Sections,1,0x1100-(ULONG)sizeof(PVOID)+1,0x2000));
    CHECK(!KexpUacDataSlot(Sections,1,0x1100,0x2000));CHECK(!KexpUacDataSlot(Sections,1,0xfff,0x2000));
    Sections[0].Characteristics=IMAGE_SCN_MEM_EXECUTE;CHECK(!KexpUacDataSlot(Sections,1,0x1000,0x2000));
    Sections[0].Characteristics=0;Sections[0].SizeOfRawData=0x200;CHECK(KexpUacDataSlot(Sections,1,0x1180,0x2000));
    Sections[1]=Sections[0];Sections[1].Characteristics=IMAGE_SCN_MEM_EXECUTE;CHECK(!KexpUacDataSlot(Sections,2,0x1000,0x2000));
    Sections[0].VirtualAddress=MAXDWORD-0x100;CHECK(!KexpUacDataSlot(Sections,1,MAXDWORD-0xff,MAXDWORD));
    TypedTeb.LastErrorValue=12345;CHECK(!KexUacGetReplacement(NULL,"ShellExecuteW")&&TypedTeb.LastErrorValue==12345&&!Locks);
    CHECK(!KexUacGetReplacement((PVOID)(ULONG_PTR)0x10000,(PCSTR)(ULONG_PTR)3)&&!Locks);
    CHECK(!KexUacGetReplacement((PVOID)(ULONG_PTR)0x10000,"UnrelatedApi")&&!Locks);
    for(Index=0;Index<ARRAYSIZE(KexpUacRoutes);++Index){
        CHECK(KexUacGetReplacement(KexpUacRoutes[Index].Native,KexpUacRoutes[Index].Name)==KexpUacRoutes[Index].Replacement);
        CHECK(TypedTeb.LastErrorValue==12345);
        CHECK(!KexUacGetReplacement((PVOID)(ULONG_PTR)0x99999,KexpUacRoutes[Index].Name));
        CHECK(TypedTeb.LastErrorValue==12345);
    }
    CHECK(Locks==Unlocks&&Resolved==Locks);
    LockFails=1;CHECK(!KexUacGetReplacement(KexpUacRoutes[0].Native,KexpUacRoutes[0].Name));
    CHECK(TypedTeb.LastErrorValue==12345&&Locks==Unlocks+1);
    printf("UAC actual route bounds/pointer gates: %d checks passed.\n",Checks);return 0;
}
