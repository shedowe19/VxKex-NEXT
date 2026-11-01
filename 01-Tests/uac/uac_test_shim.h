#ifndef UAC_TEST_SHIM_H
#define UAC_TEST_SHIM_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#else
typedef void VOID, *PVOID, *HANDLE, *HWND, *HINSTANCE;
typedef int BOOL, INT;
typedef unsigned int UINT;
typedef uint32_t ULONG, DWORD, *PULONG;
typedef int32_t LONG, HRESULT;
typedef int32_t NTSTATUS;
typedef size_t SIZE_T;
typedef unsigned short USHORT;
typedef uint64_t ULONGLONG, *PULONGLONG;
typedef uintptr_t ULONG_PTR, HCRYPTPROV;
typedef unsigned char BYTE, BOOLEAN, *PBOOLEAN;
typedef wchar_t WCHAR, *PWSTR;
typedef const wchar_t *PCWSTR;
typedef const char *PCSTR;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { BYTE Name[8]; union { DWORD PhysicalAddress,VirtualSize; } Misc; DWORD VirtualAddress,SizeOfRawData,PointerToRawData,PointerToRelocations,PointerToLinenumbers; USHORT NumberOfRelocations,NumberOfLinenumbers; DWORD Characteristics; } IMAGE_SECTION_HEADER,*PIMAGE_SECTION_HEADER;
typedef struct { DWORD dwFileAttributes; FILETIME ftCreationTime,ftLastAccessTime,ftLastWriteTime; DWORD dwVolumeSerialNumber,nFileSizeHigh,nFileSizeLow,nNumberOfLinks,nFileIndexHigh,nFileIndexLow; } BY_HANDLE_FILE_INFORMATION;
typedef unsigned short WORD;
typedef struct { HANDLE hProcess,hThread; DWORD dwProcessId,dwThreadId; } PROCESS_INFORMATION;
typedef struct { DWORD cb; PWSTR lpReserved,lpDesktop,lpTitle; DWORD dwX,dwY,dwXSize,dwYSize,dwXCountChars,dwYCountChars,dwFillAttribute,dwFlags; WORD wShowWindow,cbReserved2; BYTE *lpReserved2; HANDLE hStdInput,hStdOutput,hStdError; } STARTUPINFOW;
typedef struct { DWORD cbSize; ULONG fMask; HWND hwnd; PCWSTR lpVerb,lpFile,lpParameters,lpDirectory; INT nShow; HINSTANCE hInstApp; PVOID lpIDList; PCWSTR lpClass; PVOID hkeyClass; DWORD dwHotKey; HANDLE hIcon,hProcess; } SHELLEXECUTEINFOW;
typedef struct { DWORD cbSize; ULONG fMask; HWND hwnd; PCSTR lpVerb,lpFile,lpParameters,lpDirectory; INT nShow; HINSTANCE hInstApp; PVOID lpIDList; PCSTR lpClass; PVOID hkeyClass; DWORD dwHotKey; HANDLE hIcon,hProcess; } SHELLEXECUTEINFOA;
#define WINAPI
#define NTAPI
#define CONST const
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define MAXDWORD UINT32_MAX
#define SYNCHRONIZE 0x100000
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_FORCEMINIMIZE 11
#define SEE_MASK_NOCLOSEPROCESS 0x40
#define SEE_MASK_NOASYNC 0x100
#define SEE_MASK_FLAG_NO_UI 0x400
#define SEE_MASK_UNICODE 0x4000
#define SEE_MASK_NOZONECHECKS 0x800000
#define SE_ERR_ACCESSDENIED 5
#define HEAP_ZERO_MEMORY 8
#define PROV_RSA_FULL 1
#define CRYPT_VERIFYCONTEXT 0xF0000000U
#define CP_ACP 0
#define ERROR_SUCCESS 0
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_DATA 13
#define ERROR_INVALID_PARAMETER 87
#define ERROR_FILENAME_EXCED_RANGE 206
#define ERROR_CANCELLED 1223
#define ERROR_TIMEOUT 1460
#define ERROR_PROCESS_ABORTED 1067
#define ERROR_INVALID_STATE 5023
#define GENERIC_READ 0x80000000U
#define GENERIC_WRITE 0x40000000U
#define OPEN_EXISTING 3
#define FILE_FLAG_OVERLAPPED 0x40000000U
#define SECURITY_SQOS_PRESENT 0x100000U
#define SECURITY_IDENTIFICATION 0x10000U
#define WAIT_TIMEOUT 258
#define CREATE_SUSPENDED 4
#define STARTF_USESHOWWINDOW 1
#define STILL_ACTIVE 259
#define IMAGE_SCN_MEM_EXECUTE 0x20000000U
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
#define KEXAPI
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status)>=0)
#endif
#ifndef STATUS_ALREADY_INITIALIZED
#define STATUS_ALREADY_INITIALIZED ((NTSTATUS)0x60000001L)
#endif
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif
#ifndef C_ASSERT
#define C_ASSERT_JOIN_(a,b) a##b
#define C_ASSERT_JOIN(a,b) C_ASSERT_JOIN_(a,b)
#define C_ASSERT(c) typedef char C_ASSERT_JOIN(assert_line_,__LINE__)[(c)?1:-1]
#endif
#ifndef FAILED
#define FAILED(hr) ((HRESULT)(hr)<0)
#endif
#ifndef ZeroMemory
#define ZeroMemory(p,n) memset((p),0,(n))
#endif
#include "production_types.h"
typedef struct { unsigned short Length,MaximumLength; PWSTR Buffer; } TEST_UNICODE_STRING;
typedef struct { KEX_IFEO_PARAMETERS IfeoParameters; TEST_UNICODE_STRING WinDir,KexDir,Kex3264DirPath; } KEX_PROCESS_DATA, *PKEX_PROCESS_DATA;
static int Checks;
static void TestCheck(int Condition,const char *Expression,int Line)
{ ++Checks; if(!Condition){fprintf(stderr,"line %d: %s\n",Line,Expression);exit(1);} }
#define CHECK(c) TestCheck(!!(c),#c,__LINE__)
#endif
