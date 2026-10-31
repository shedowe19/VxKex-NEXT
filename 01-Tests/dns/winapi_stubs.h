#ifndef DNS_WINAPI_STUBS_H
#define DNS_WINAPI_STUBS_H
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#define IN
#define OUT
#define OPTIONAL
#define STATIC static
#define VOLATILE volatile
#define CALLBACK
#define WINAPI
#define WSAAPI
#define KXNETAPI
#define TRUE 1
#define FALSE 0
#define TYPEDEF_TYPE_NAME(t) t, *P##t
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define unless(x) if (!(x))
#define until(x) while (!(x))
#define ASSERT(x) assert(x)
#define KexLogDebugEvent(...) ((void) 0)
#define KexDebugCheckpoint() ((void) 0)
#define KexRtlZeroMemory(p,n) memset((p), 0, (n))
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
#define NO_ERROR 0
#define WSA_INVALID_HANDLE 6
#define WSA_NOT_ENOUGH_MEMORY 8
#define WSA_IO_PENDING 997
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEINPROGRESS 10036
#define WSAEAFNOSUPPORT 10047
#define WSAETIMEDOUT 10060
#define WSAENAMETOOLONG 10063
#define WSA_E_CANCELLED 10111
#define WSASYSCALLFAILURE 10107
#define WSAHOST_NOT_FOUND 11001
#define WSANO_RECOVERY 11003
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 23
#define AI_CANONNAME 2
#define AI_FQDN 0x20000
#define INVALID_HANDLE_VALUE ((HANDLE) (uintptr_t) -1)
typedef void VOID;
typedef void *PVOID;
typedef uint8_t UCHAR;
typedef uint8_t BOOLEAN;
typedef int BOOL;
typedef int INT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uintptr_t ULONG_PTR;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef int32_t HRESULT;
typedef wchar_t WCHAR;
typedef const WCHAR *PCWSTR;
typedef void *HANDLE;
typedef HANDLE *PHANDLE;
typedef struct { unsigned char Data[16]; } GUID, *LPGUID;
typedef struct { LONG tv_sec; LONG tv_usec; } TIMEVAL, *PTIMEVAL;
typedef struct _ADDRINFOEXW {
    int ai_flags;
    int ai_family;
    size_t ai_addrlen;
    void *ai_canonname;
    void *ai_addr;
    struct _ADDRINFOEXW *ai_next;
} ADDRINFOEXW, *PADDRINFOEXW, **PPADDRINFOEXW;
typedef const ADDRINFOEXW *PCADDRINFOEXW;
typedef struct { ULONG_PTR Internal; PVOID Pointer; HANDLE hEvent; } OVERLAPPED, *LPOVERLAPPED;
typedef void (*LPLOOKUPSERVICE_COMPLETION_ROUTINE)(ULONG, ULONG, LPOVERLAPPED);
typedef union { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { uint32_t Low; uint32_t High; } FILETIME, *PFILETIME;
typedef pthread_mutex_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
typedef void *PTP_CALLBACK_INSTANCE;
typedef struct { int Unused; } TP_CALLBACK_ENVIRON;
typedef struct MOCK_TIMER *PTP_TIMER;
typedef void (*MOCK_WORK_CALLBACK)(PTP_CALLBACK_INSTANCE, PVOID);
typedef void (*MOCK_TIMER_CALLBACK)(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER);
static LONG InterlockedIncrement(volatile LONG *p) { return __sync_add_and_fetch(p, 1); }
static LONG InterlockedDecrement(volatile LONG *p) { return __sync_sub_and_fetch(p, 1); }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { return __sync_lock_test_and_set(p, v); }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG c) { return __sync_val_compare_and_swap(p, c, v); }
static void AcquireSRWLockExclusive(SRWLOCK *p) { assert(pthread_mutex_lock(p) == 0); }
static void ReleaseSRWLockExclusive(SRWLOCK *p) { assert(pthread_mutex_unlock(p) == 0); }
static void InitializeThreadpoolEnvironment(TP_CALLBACK_ENVIRON *p) { p->Unused = 0; }
static void SetThreadpoolCallbackRunsLong(TP_CALLBACK_ENVIRON *p) { (void) p; }
static void DestroyThreadpoolEnvironment(TP_CALLBACK_ENVIRON *p) { (void) p; }
static void SetLastError(ULONG Error);
static PVOID GetProcessHeap(void);
static PVOID HeapAlloc(PVOID Heap, ULONG Flags, size_t Size);
static BOOL HeapFree(PVOID Heap, ULONG Flags, PVOID Memory);
static BOOL SetEvent(HANDLE Event);
static HRESULT StringCchCopy(WCHAR *Destination, size_t Count, PCWSTR Source);
static BOOL TrySubmitThreadpoolCallback(MOCK_WORK_CALLBACK Callback, PVOID Parameter, TP_CALLBACK_ENVIRON *Environment);
static PTP_TIMER CreateThreadpoolTimer(MOCK_TIMER_CALLBACK Callback, PVOID Parameter, TP_CALLBACK_ENVIRON *Environment);
static void SetThreadpoolTimer(PTP_TIMER Timer, PFILETIME DueTime, ULONG Period, ULONG Window);
static void WaitForThreadpoolTimerCallbacks(PTP_TIMER Timer, BOOL CancelPending);
static void CloseThreadpoolTimer(PTP_TIMER Timer);
static INT GetAddrInfoExW(PCWSTR Name, PCWSTR ServiceName, ULONG Namespace, LPGUID Guid,
    PCADDRINFOEXW Hints, PPADDRINFOEXW Result, PTIMEVAL Timeout, LPOVERLAPPED Overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE Completion, PHANDLE Cancel);
static void FreeAddrInfoExW(PADDRINFOEXW Result);
#endif
