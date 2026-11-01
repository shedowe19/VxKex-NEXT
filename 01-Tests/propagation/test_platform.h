#ifndef VXKEX_PROPAGATION_TEST_PLATFORM_H
#define VXKEX_PROPAGATION_TEST_PLATFORM_H

#include <stddef.h>

/* Keep the Windows NT fixed-width fields on both Windows and portable hosts. */
typedef unsigned char BYTE;
typedef BYTE *PBYTE;
typedef unsigned short USHORT;
typedef unsigned int ULONG;
typedef ULONG *PULONG;
typedef size_t ULONG_PTR;
typedef size_t SIZE_T;
typedef SIZE_T *PSIZE_T;
typedef void *PVOID;
typedef PVOID *PPVOID;
typedef PVOID HANDLE;
typedef int NTSTATUS;
#ifdef _MSC_VER
typedef unsigned __int64 ULONGLONG;
#define NTAPI __stdcall
#else
typedef unsigned long long ULONGLONG;
#define NTAPI
#endif
typedef ULONGLONG *PULONGLONG;

#define STATIC static
#define VOID void
#define KEXDATA_FLAG_PROPAGATED 0x00000001U
#define STATUS_SUCCESS 0
#define NT_SUCCESS(status) ((status) >= 0)
#define MEM_RELEASE 0x8000U
#define PAGE_EXECUTE_WRITECOPY 0x80U
#define LOWORD(value) ((USHORT)((value) & 0xffffU))
#define ASSERT assert
#define unless(condition) if (!(condition))

#endif
