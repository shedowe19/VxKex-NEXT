#ifndef KEX_SERVICE_TEST_SHIM_H
#define KEX_SERVICE_TEST_SHIM_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winsvc.h>
#include <objbase.h>
#else
typedef void VOID, *PVOID, *HLOCAL, *SC_HANDLE, *HANDLE;
typedef int BOOL, INT;
typedef uint32_t DWORD, ULONG, *LPDWORD;
typedef uintptr_t ULONG_PTR;
typedef unsigned char BYTE, UCHAR, BOOLEAN, *PBOOLEAN, *PBYTE;
typedef wchar_t WCHAR, *PWSTR, *LPWSTR;
typedef const wchar_t *PCWSTR, *LPCWSTR;
typedef const char *PCSTR, *LPCSTR;
typedef size_t SIZE_T;
typedef void *HKEY;
typedef int32_t HRESULT, NTSTATUS, LONG;
typedef struct { uint32_t Data1; uint16_t Data2,Data3; uint8_t Data4[8]; } CLSID;
typedef const CLSID *REFCLSID;
typedef struct { int held; } SRWLOCK, *PSRWLOCK;
typedef struct { DWORD dwServiceType,dwCurrentState,dwControlsAccepted,dwWin32ExitCode,dwServiceSpecificExitCode,dwCheckPoint,dwWaitHint,dwProcessId,dwServiceFlags; } SERVICE_STATUS_PROCESS;
typedef struct { DWORD dwServiceType,dwStartType,dwErrorControl; LPWSTR lpBinaryPathName,lpLoadOrderGroup; DWORD dwTagId; LPWSTR lpDependencies,lpServiceStartName,lpDisplayName; } QUERY_SERVICE_CONFIGW, *LPQUERY_SERVICE_CONFIGW;
#define WINAPI
#define IN
#define OUT
#define OPTIONAL
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define SRWLOCK_INIT {0}
#define CP_ACP 0
#define SERVICES_ACTIVE_DATABASEW L"ServicesActive"
#define SERVICES_ACTIVE_DATABASEA "ServicesActive"
#define SC_MANAGER_CONNECT 1
#define SERVICE_QUERY_CONFIG 1
#define SERVICE_QUERY_STATUS 4
#define SERVICE_START 16
#define GENERIC_EXECUTE 0x20000000U
#define GENERIC_ALL 0x10000000U
#define MAXIMUM_ALLOWED 0x02000000U
#define SC_STATUS_PROCESS_INFO 0
#define SERVICE_STOPPED 1
#define SERVICE_START_PENDING 2
#define SERVICE_RUNNING 4
#define SERVICE_WIN32_OWN_PROCESS 0x10
#define SERVICE_WIN32_SHARE_PROCESS 0x20
#define SERVICE_INTERACTIVE_PROCESS 0x100
#define SERVICE_KERNEL_DRIVER 1
#define SERVICE_DISABLED 4
#define SERVICE_DEMAND_START 3
#define ERROR_SUCCESS 0
#define ERROR_ACCESS_DENIED 5
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_DATA 13
#define ERROR_NOT_SUPPORTED 50
#define ERROR_GEN_FAILURE 31
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_INVALID_NAME 123
#define ERROR_FILENAME_EXCED_RANGE 206
#define ERROR_DIRECTORY 267
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_ALREADY_EXISTS 183
#define ERROR_BAD_PATHNAME 161
#define REG_DWORD 4
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_QUERY_VALUE 1
#define KEY_WOW64_32KEY 0x0200
#define KEY_WOW64_64KEY 0x0100
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002U)
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001U)
#define CLSCTX_LOCAL_SERVER 4
#define CLSCTX_REMOTE_SERVER 16
#define CLSCTX_ACTIVATE_32_BIT_SERVER 0x40000
#define CLSCTX_ACTIVATE_64_BIT_SERVER 0x80000
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005U)
#define FAILED(x) ((HRESULT)(x)<0)
#define ERROR_SERVICE_ALREADY_RUNNING 1056
#define ERROR_SERVICE_DISABLED 1058
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400
#define FILE_READ_ATTRIBUTES 0x80
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define FILE_NAME_NORMALIZED 0
#define VOLUME_NAME_DOS 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define LPTR 0x40
#define CSTR_LESS_THAN 1
#define CSTR_EQUAL 2
#define CSTR_GREATER_THAN 3
#endif
#define STATIC static
#define NOTHING ((void)0)
#define KXCFGAPI WINAPI
#define KXCFGDECLSPEC
#ifndef CONST
#define CONST const
#endif
#define NT_SUCCESS(x) ((NTSTATUS)(x)>=0)
#define FLG_APPLICATION_VERIFIER 0x100
#define KEX_STRONGSPOOF_VALID_MASK 3
#define KEXDATA_FLAG_PROPAGATED 1
#define KEXDATA_FLAG_IFEO_OPTIONS_PRESENT 2
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif
#define RtlZeroMemory(p,n) memset((p),0,(n))
/* Force signedness on portable compilers to model the VC10 enum's ABI. */
typedef enum { TestSignedEnum=-1,WinVerSpoofNone,WinVerSpoofWin7,WinVerSpoofWin8,WinVerSpoofWin8Point1,WinVerSpoofWin10,WinVerSpoofWin11,WinVerSpoofMax } KEX_WIN_VER_SPOOF;
typedef struct { ULONG DisableForChild,DisableAppSpecific; KEX_WIN_VER_SPOOF WinVerSpoof; ULONG StrongVersionSpoof,DisableConsoleEnhancements,TlsForceEnabledProtocols,TlsForceDisabledProtocols; } KEX_IFEO_PARAMETERS;
typedef struct { ULONG Flags; KEX_IFEO_PARAMETERS IfeoParameters; HANDLE LogHandle; } TEST_KEX_PROCESS_DATA;
extern TEST_KEX_PROCESS_DATA *KexData;

/* All operating-system calls in the two compiled production units are doubled. */
#define GetLastError MockGetLastError
#define SetLastError MockSetLastError
#define AcquireSRWLockExclusive MockAcquireExclusive
#define ReleaseSRWLockExclusive MockReleaseExclusive
#define AcquireSRWLockShared MockAcquireShared
#define ReleaseSRWLockShared MockReleaseShared
#define MultiByteToWideChar MockMultiByteToWideChar
#define OpenSCManagerW MockOpenSCManagerW
#define OpenSCManagerA MockOpenSCManagerA
#define OpenServiceW MockOpenServiceW
#define OpenServiceA MockOpenServiceA
#define CreateServiceW MockCreateServiceW
#define CreateServiceA MockCreateServiceA
#define CloseServiceHandle MockCloseServiceHandle
#define StartServiceW MockStartServiceW
#define StartServiceA MockStartServiceA
#define QueryServiceStatusEx MockQueryServiceStatusEx
#define QueryServiceConfigW MockQueryServiceConfigW
#define LocalAlloc MockLocalAlloc
#define LocalFree MockLocalFree
#define GetFileAttributesW MockGetFileAttributesW
#define GetWindowsDirectoryW MockGetWindowsDirectoryW
#define GetFullPathNameW MockGetFullPathNameW
#define CompareStringOrdinal MockCompareStringOrdinal
#define CreateFileW MockCreateFileW
#define GetFinalPathNameByHandleW MockGetFinalPathNameByHandleW
#define CloseHandle MockCloseHandle
#define CreateSimpleTransaction MockCreateSimpleTransaction
#define NtCommitTransaction MockNtCommitTransaction
#define NtRollbackTransaction MockNtRollbackTransaction
#define NtClose MockNtClose
#define RtlNtStatusToDosError MockRtlNtStatusToDosError
#define RegCreateKeyTransactedW MockRegCreateKeyTransactedW
#define RegOpenKeyTransactedW MockRegOpenKeyTransactedW
#define RegEnumValueW MockRegEnumValueW
#define RegEnumKeyExW MockRegEnumKeyExW
#define RegQueryValueExW MockRegQueryValueExW
#define RegSetValueExW MockRegSetValueExW
#define RegCloseKey MockRegCloseKey
#define RegOpenKeyExW MockRegOpenKeyExW
#define StringCchPrintf MockStringCchPrintf
#define StringFromGUID2 MockStringFromGUID2
#define CLSIDFromString MockCLSIDFromString
#define KexLogInformationEvent MockInformationEvent
#define LdrQueryImageFileKeyOption MockLdrQueryImageFileKeyOption
#define KexDataInitialize MockKexDataInitialize
#define VxlWriteLog MockVxlWriteLog
#define KEX_COMPONENT L"KxAdvapi"
#define LogSeverityInformation 3
typedef TEST_KEX_PROCESS_DATA *PKEX_PROCESS_DATA;
DWORD WINAPI MockGetLastError(VOID);
VOID WINAPI MockSetLastError(DWORD);
VOID WINAPI MockAcquireExclusive(PSRWLOCK);
VOID WINAPI MockReleaseExclusive(PSRWLOCK);
VOID WINAPI MockAcquireShared(PSRWLOCK);
VOID WINAPI MockReleaseShared(PSRWLOCK);
INT WINAPI MockMultiByteToWideChar(unsigned int,DWORD,LPCSTR,INT,LPWSTR,INT);
SC_HANDLE WINAPI MockOpenSCManagerW(LPCWSTR,LPCWSTR,DWORD);
SC_HANDLE WINAPI MockOpenSCManagerA(LPCSTR,LPCSTR,DWORD);
SC_HANDLE WINAPI MockOpenServiceW(SC_HANDLE,LPCWSTR,DWORD);
SC_HANDLE WINAPI MockOpenServiceA(SC_HANDLE,LPCSTR,DWORD);
SC_HANDLE WINAPI MockCreateServiceW(SC_HANDLE,LPCWSTR,LPCWSTR,DWORD,DWORD,DWORD,DWORD,LPCWSTR,LPCWSTR,LPDWORD,LPCWSTR,LPCWSTR,LPCWSTR);
SC_HANDLE WINAPI MockCreateServiceA(SC_HANDLE,LPCSTR,LPCSTR,DWORD,DWORD,DWORD,DWORD,LPCSTR,LPCSTR,LPDWORD,LPCSTR,LPCSTR,LPCSTR);
BOOL WINAPI MockCloseServiceHandle(SC_HANDLE);
BOOL WINAPI MockStartServiceW(SC_HANDLE,DWORD,LPCWSTR*);
BOOL WINAPI MockStartServiceA(SC_HANDLE,DWORD,LPCSTR*);
BOOL WINAPI MockQueryServiceStatusEx(SC_HANDLE,int,PBYTE,DWORD,LPDWORD);
BOOL WINAPI MockQueryServiceConfigW(SC_HANDLE,LPQUERY_SERVICE_CONFIGW,DWORD,LPDWORD);
HLOCAL WINAPI MockLocalAlloc(unsigned int,SIZE_T);
HLOCAL WINAPI MockLocalFree(HLOCAL);
DWORD WINAPI MockGetFileAttributesW(LPCWSTR);
unsigned int WINAPI MockGetWindowsDirectoryW(LPWSTR,unsigned int);
DWORD WINAPI MockGetFullPathNameW(LPCWSTR,DWORD,LPWSTR,LPWSTR*);
INT WINAPI MockCompareStringOrdinal(LPCWSTR,INT,LPCWSTR,INT,BOOL);
HANDLE WINAPI MockCreateFileW(LPCWSTR,DWORD,DWORD,PVOID,DWORD,DWORD,HANDLE);
DWORD WINAPI MockGetFinalPathNameByHandleW(HANDLE,LPWSTR,DWORD,DWORD);
BOOL WINAPI MockCloseHandle(HANDLE);
BOOLEAN StringEqualI(PCWSTR,PCWSTR);
HRESULT MockStringCchPrintf(PWSTR,size_t,PCWSTR,...);
HANDLE MockCreateSimpleTransaction(PCWSTR);
NTSTATUS WINAPI MockNtCommitTransaction(HANDLE,BOOLEAN);
NTSTATUS WINAPI MockNtRollbackTransaction(HANDLE,BOOLEAN);
NTSTATUS WINAPI MockNtClose(HANDLE);
ULONG WINAPI MockRtlNtStatusToDosError(NTSTATUS);
LONG WINAPI MockRegCreateKeyTransactedW(HKEY,PCWSTR,DWORD,PWSTR,DWORD,DWORD,PVOID,HKEY*,LPDWORD,HANDLE,PVOID);
LONG WINAPI MockRegOpenKeyTransactedW(HKEY,PCWSTR,DWORD,DWORD,HKEY*,HANDLE,PVOID);
LONG WINAPI MockRegEnumValueW(HKEY,DWORD,PWSTR,LPDWORD,LPDWORD,LPDWORD,PBYTE,LPDWORD);
LONG WINAPI MockRegEnumKeyExW(HKEY,DWORD,PWSTR,LPDWORD,LPDWORD,PWSTR,LPDWORD,PVOID);
LONG WINAPI MockRegQueryValueExW(HKEY,PCWSTR,LPDWORD,LPDWORD,PBYTE,LPDWORD);
LONG WINAPI MockRegSetValueExW(HKEY,PCWSTR,DWORD,DWORD,const BYTE*,DWORD);
LONG WINAPI MockRegCloseKey(HKEY);
LONG WINAPI MockRegOpenKeyExW(HKEY,PCWSTR,DWORD,DWORD,HKEY*);
INT WINAPI MockStringFromGUID2(REFCLSID,PWSTR,INT);
HRESULT WINAPI MockCLSIDFromString(PWSTR,CLSID*);
VOID MockInformationEvent(PCWSTR,...);
VOID WINAPI KxCompTryInheritServiceForClass(REFCLSID,ULONG);
NTSTATUS WINAPI MockLdrQueryImageFileKeyOption(HANDLE,PCWSTR,ULONG,PVOID,ULONG,ULONG*);
NTSTATUS WINAPI MockKexDataInitialize(TEST_KEX_PROCESS_DATA**);
VOID MockVxlWriteLog(HANDLE,PCWSTR,ULONG,PCWSTR,...);
ULONG KXCFGAPI KxCfgTryInheritConfiguration(PCWSTR,const KEX_IFEO_PARAMETERS*,PBOOLEAN);
ULONG KXCFGAPI KxCfgTryInheritServiceParameters(PCWSTR,const KEX_IFEO_PARAMETERS*,PBOOLEAN);
VOID WINAPI KxAdvapiTryInheritService(PCWSTR);
VOID KxAdvapiResetServiceTrackingForTest(VOID);
SC_HANDLE WINAPI Ext_OpenSCManagerW(LPCWSTR,LPCWSTR,DWORD);
SC_HANDLE WINAPI Ext_OpenSCManagerA(LPCSTR,LPCSTR,DWORD);
SC_HANDLE WINAPI Ext_OpenServiceW(SC_HANDLE,LPCWSTR,DWORD);
SC_HANDLE WINAPI Ext_OpenServiceA(SC_HANDLE,LPCSTR,DWORD);
SC_HANDLE WINAPI Ext_CreateServiceW(SC_HANDLE,LPCWSTR,LPCWSTR,DWORD,DWORD,DWORD,DWORD,LPCWSTR,LPCWSTR,LPDWORD,LPCWSTR,LPCWSTR,LPCWSTR);
SC_HANDLE WINAPI Ext_CreateServiceA(SC_HANDLE,LPCSTR,LPCSTR,DWORD,DWORD,DWORD,DWORD,LPCSTR,LPCSTR,LPDWORD,LPCSTR,LPCSTR,LPCSTR);
BOOL WINAPI Ext_CloseServiceHandle(SC_HANDLE);
BOOL WINAPI Ext_StartServiceW(SC_HANDLE,DWORD,LPCWSTR*);
BOOL WINAPI Ext_StartServiceA(SC_HANDLE,DWORD,LPCSTR*);
#endif
