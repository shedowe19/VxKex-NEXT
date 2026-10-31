#ifndef VXKEX_CHROME_TEST_PLATFORM_H
#define VXKEX_CHROME_TEST_PLATFORM_H

#include <stddef.h>
#include <wchar.h>

#ifdef _WIN32
/* On Windows, retain the SDK's DWORD/handle sizes and x86 WINAPI convention. */
#include <windows.h>
#include <ncrypt.h>
#else
typedef int BOOL;
typedef unsigned int DWORD;
typedef unsigned int ULONG;
typedef size_t ULONG_PTR;
typedef ptrdiff_t INT_PTR;
typedef char CHAR;
typedef const char *PCSTR;
typedef void *HMODULE;
typedef unsigned char BYTE;
typedef BYTE *PBYTE;
typedef int SECURITY_STATUS;
typedef ULONG_PTR NCRYPT_KEY_HANDLE;
typedef struct _NCryptBuffer {
    ULONG cbBuffer;
    ULONG BufferType;
    void *pvBuffer;
} NCryptBuffer;
typedef struct _NCryptBufferDesc {
    ULONG ulVersion;
    ULONG cBuffers;
    NCryptBuffer *pBuffers;
} NCryptBufferDesc;
#define WINAPI
typedef INT_PTR (WINAPI *FARPROC)(void);
#define ERROR_PROC_NOT_FOUND 127
#define NTE_NOT_SUPPORTED ((SECURITY_STATUS)0x80090029U)
#endif

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(value) ((void)(value))
#endif
#define KXBASEAPI
#define KXCRYPAPI

#endif
