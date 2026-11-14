/* Test-only header for compiling the unchanged production function body.
 * Imports are real native APIs. Only the initial class58 availability result
 * can be forced to INVALID_INFO_CLASS, so modern hosts exercise the fallback. */
#define WIN32_NO_STATUS
#include <windows.h>
#include <intrin.h>
#pragma warning(disable:4127) /* SafeFree's conventional do/while(0) macro. */
#undef WIN32_NO_STATUS
#include <ntstatus.h>
typedef LONG NTSTATUS;
typedef ULONG PROCESSINFOCLASS;
typedef struct { NTSTATUS ExitStatus; PVOID Peb; ULONG_PTR Affinity; LONG Priority;
    ULONG_PTR UniqueProcessId; ULONG_PTR Parent; } PROCESS_BASIC_INFORMATION;
typedef struct { ULONG Attributes,GrantedAccess,HandleCount,PointerCount;
    ULONG PagedPoolCharge,NonPagedPoolCharge,Reserved[3],NameInfoSize,TypeInfoSize,SecurityInfoSize;
    LARGE_INTEGER CreationTime; } OBJECT_BASIC_INFORMATION;
typedef struct {LONGLONG CreateTime,ExitTime,KernelTime,UserTime;} KERNEL_USER_TIMES;
#ifdef _WIN64
typedef struct { BYTE Reserved[0x40]; struct { HANDLE UniqueProcess,UniqueThread; } ClientId; } TEST_TEB;
#define NtCurrentTeb() ((TEST_TEB*)__readgsqword(0x30))
#else
typedef struct { BYTE Reserved[0x20]; struct { HANDLE UniqueProcess,UniqueThread; } ClientId; } TEST_TEB;
#define NtCurrentTeb() ((TEST_TEB*)__readfsdword(0x18))
#endif
#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
#define NT_SUCCESS(x) (((NTSTATUS)(x))>=0)
#define PAGE_SIZE 4096
#define MAXULONG ((ULONG)0xffffffff)
#define ProcessBasicInformation 0
#define ProcessTimes 4
#define SystemExtendedHandleInformation 64
#define ObjectBasicInformation 0
#define RtlGetLastWin32Error GetLastError
#define SafeAlloc(Type,Count) ((Type*)RtlAllocateHeap(GetProcessHeap(),0,sizeof(Type)*(Count)))
#define SafeFree(Pointer) do {RtlFreeHeap(GetProcessHeap(),0,(Pointer));(Pointer)=NULL;} while(0)
__declspec(dllimport) PVOID NTAPI RtlAllocateHeap(HANDLE,ULONG,SIZE_T);
__declspec(dllimport) BOOLEAN NTAPI RtlFreeHeap(HANDLE,ULONG,PVOID);
__declspec(dllimport) VOID NTAPI RtlSetLastWin32Error(ULONG);
__declspec(dllimport) NTSTATUS NTAPI NtDuplicateObject(HANDLE,HANDLE,HANDLE,PHANDLE,ACCESS_MASK,ULONG,ULONG);
__declspec(dllimport) NTSTATUS NTAPI NtQueryObject(HANDLE,ULONG,PVOID,ULONG,PULONG);
__declspec(dllimport) NTSTATUS NTAPI NtQuerySystemInformation(ULONG,PVOID,ULONG,PULONG);
__declspec(dllimport) NTSTATUS NTAPI NtClose(HANDLE);
NTSTATUS NTAPI TestQueryInformationProcess(HANDLE,ULONG,PVOID,ULONG,PULONG);
#define NtQueryInformationProcess TestQueryInformationProcess
