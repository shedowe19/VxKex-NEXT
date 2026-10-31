#ifndef VXKEX_BLUETOOTH_MOCK_WINDOWS_H
#define VXKEX_BLUETOOTH_MOCK_WINDOWS_H

#include <stddef.h>
#include <wchar.h>

#define KXUSERAPI
#define WINAPI
#define CALLBACK
#define STATIC static
#define IN
#define OUT
#define OPTIONAL
#define TRUE 1
#define FALSE 0
#define UNREFERENCED_PARAMETER(value) ((void) (value))

typedef void VOID;
typedef void *PVOID;
typedef void *HANDLE;
typedef void *HMODULE;
typedef int BOOL;
typedef int HRESULT;
typedef unsigned int ULONG;
typedef unsigned short USHORT;
typedef USHORT *PUSHORT;
typedef void (*FARPROC)(void);

typedef struct _INIT_ONCE {
	int Completed;
} INIT_ONCE, *PINIT_ONCE;

#define INIT_ONCE_STATIC_INIT {0}
#define ERROR_NOT_SUPPORTED 50
#define ERROR_MOD_NOT_FOUND 126
#define ERROR_PROC_NOT_FOUND 127

typedef BOOL (CALLBACK *PINIT_ONCE_FN)(PINIT_ONCE, PVOID, PVOID *);

BOOL InitOnceExecuteOnce(PINIT_ONCE Once, PINIT_ONCE_FN Callback,
	PVOID Parameter, PVOID *Context);
HMODULE LoadSystemLibrary(const wchar_t *FileName);
FARPROC GetProcAddress(HMODULE Module, const char *Name);
void SetLastError(unsigned int Error);
unsigned int GetLastError(void);

#endif
