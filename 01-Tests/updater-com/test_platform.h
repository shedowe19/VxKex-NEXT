#ifndef VXKEX_UPDATER_COM_TEST_PLATFORM_H
#define VXKEX_UPDATER_COM_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>

#ifdef _WIN32
/* Keep the real SDK's handles, token layouts and x86 WINAPI convention. */
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
typedef PVOID *PPVOID;
typedef PWSTR *PPWSTR;
#else
typedef int BOOL;
typedef unsigned char BOOLEAN, UCHAR;
typedef unsigned int ULONG, DWORD;
typedef int32_t LONG, HRESULT, NTSTATUS;
typedef uintptr_t ULONG_PTR;
typedef int INT;
typedef void VOID, *PVOID, **PPVOID, *HANDLE, **PHANDLE, *HKEY;
typedef wchar_t WCHAR, *PWSTR, *LPOLESTR;
typedef const wchar_t *PCWSTR;
typedef PWSTR *PPWSTR;
typedef unsigned char *PBYTE;
typedef void *LPUNKNOWN, *PSID;
typedef struct { uint32_t Data1; uint16_t Data2, Data3; unsigned char Data4[8]; } CLSID, IID;
typedef const CLSID *REFCLSID;
typedef const IID *REFIID;
typedef struct { DWORD TokenIsElevated; } TOKEN_ELEVATION;
typedef struct { struct { PSID Sid; DWORD Attributes; } Label; } TOKEN_MANDATORY_LABEL, *PTOKEN_MANDATORY_LABEL;
typedef struct { ULONG cb; } STARTUPINFOW;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define IN
#define OUT
#define STATIC static
#define CONST const
#define MAX_PATH 260
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_ACCESS_DENIED 5
#define ERROR_BAD_EXE_FORMAT 193
#define ERROR_NO_TOKEN 1008
#define ERROR_MORE_DATA 234
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define KEY_READ 0x20019
#define KEY_WOW64_32KEY 0x0200
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001U)
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002U)
#define CLSCTX_LOCAL_SERVER 4
#define SECURITY_MANDATORY_MEDIUM_RID 8192
#define TOKEN_QUERY 8
#define TokenElevation 20
#define TokenIntegrityLevel 25
#define CSIDL_LOCAL_APPDATA 28
#define SHGFP_TYPE_CURRENT 0
#define DRIVE_FIXED 3
#define INVALID_FILE_ATTRIBUTES ((ULONG)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400
#define FILE_ATTRIBUTE_NORMAL 0x80
#define GENERIC_READ 0x80000000U
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define FILE_NAME_NORMALIZED 0
#define VOLUME_NAME_DOS 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005U)
#define E_NOINTERFACE ((HRESULT)0x80004002U)
#define HRESULT_FROM_WIN32(x) ((HRESULT)(0x80070000U | (unsigned int)(x)))
#define SUCCEEDED(x) ((HRESULT)(x) >= 0)
#define FAILED(x) ((HRESULT)(x) < 0)
#define IsEqualGUID(a,b) (memcmp((a),(b),sizeof(CLSID)) == 0)
#endif

#ifndef STATIC
#define STATIC static
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)
#define KXCOMAPI
static int KexIsDebugBuild = 0;
#define LogSeverityDebug 0
#define LogSeverityWarning 1
#define WinVerSpoofWin10 4
#define RtlZeroMemory(p,n) memset((p),0,(n))

typedef struct { PCWSTR Buffer; } UNICODE_STRING;
typedef struct {
    BOOLEAN Enabled;
    ULONG DisableForChild, DisableAppSpecificHacks, WinVerSpoof, StrongSpoofOptions;
} KXCFG_PROGRAM_CONFIGURATION;
typedef struct {
    struct { ULONG DisableForChild, DisableAppSpecific, StrongVersionSpoof; } IfeoParameters;
} TEST_KEX_DATA;
typedef struct { ULONG NtMajorVersion, NtMinorVersion; } TEST_SHARED_DATA;

#endif
