///////////////////////////////////////////////////////////////////////////////
//
// Local service-handle provenance for service configuration inheritance.
// Only the service explicitly requested by a VxKex application is considered.
// Native SCM arguments, requested rights, return values and errors are retained.
//
///////////////////////////////////////////////////////////////////////////////

#ifdef KEX_SERVICE_TEST
#include "service_test_shim.h"
#else
#include "buildcfg.h"
#undef NOSERVICE
#include <KexComm.h>
#endif

VOID WINAPI KxAdvapiTryInheritService(IN PCWSTR ServiceName);

#define KXADVAPI_SERVICE_TRACKING_CAPACITY 128
#define KXADVAPI_SERVICE_NAME_CAPACITY 257
#define KXADVAPI_SERVICE_ANSI_CAPACITY 1025
#define KXADVAPI_SCM_HANDLE 1
#define KXADVAPI_SERVICE_HANDLE 2

typedef struct _KXADVAPI_TRACKED_SERVICE_HANDLE {
	SC_HANDLE Handle;
	ULONG Kind;
	DWORD DesiredAccess;
	WCHAR ServiceName[KXADVAPI_SERVICE_NAME_CAPACITY];
} KXADVAPI_TRACKED_SERVICE_HANDLE;

STATIC SRWLOCK KxAdvapiServiceTrackingLock = SRWLOCK_INIT;
STATIC KXADVAPI_TRACKED_SERVICE_HANDLE
	KxAdvapiServiceHandles[KXADVAPI_SERVICE_TRACKING_CAPACITY];

STATIC BOOLEAN KxAdvapiAsciiEqualIW(IN PCWSTR Left, IN PCWSTR Right)
{
	WCHAR LeftCharacter;
	WCHAR RightCharacter;

	while (*Left && *Right) {
		LeftCharacter = *Left++;
		RightCharacter = *Right++;
		if (LeftCharacter >= L'A' && LeftCharacter <= L'Z') {
			LeftCharacter += L'a' - L'A';
		}
		if (RightCharacter >= L'A' && RightCharacter <= L'Z') {
			RightCharacter += L'a' - L'A';
		}
		if (LeftCharacter != RightCharacter) {
			return FALSE;
		}
	}

	return *Left == *Right;
}

STATIC BOOLEAN KxAdvapiAsciiEqualIA(IN PCSTR Left, IN PCSTR Right)
{
	UCHAR LeftCharacter;
	UCHAR RightCharacter;

	while (*Left && *Right) {
		LeftCharacter = (UCHAR) *Left++;
		RightCharacter = (UCHAR) *Right++;
		if (LeftCharacter >= 'A' && LeftCharacter <= 'Z') {
			LeftCharacter += 'a' - 'A';
		}
		if (RightCharacter >= 'A' && RightCharacter <= 'Z') {
			RightCharacter += 'a' - 'A';
		}
		if (LeftCharacter != RightCharacter) {
			return FALSE;
		}
	}

	return *Left == *Right;
}

STATIC BOOLEAN KxAdvapiCaptureServiceNameW(
	IN PCWSTR ServiceName,
	OUT PWSTR CapturedName)
{
	ULONG Index;

	if (ServiceName == NULL || ServiceName[0] == L'\0') {
		return FALSE;
	}

	for (Index = 0; Index < KXADVAPI_SERVICE_NAME_CAPACITY; ++Index) {
		CapturedName[Index] = ServiceName[Index];
		if (CapturedName[Index] == L'\0') {
			return TRUE;
		}
	}

	return FALSE;
}

STATIC BOOLEAN KxAdvapiCaptureServiceNameA(
	IN PCSTR ServiceName,
	OUT PWSTR CapturedName)
{
	ULONG Length;
	INT Characters;

	if (ServiceName == NULL || ServiceName[0] == '\0') {
		return FALSE;
	}

	for (Length = 0; Length < KXADVAPI_SERVICE_ANSI_CAPACITY; ++Length) {
		if (ServiceName[Length] == '\0') {
			Characters = MultiByteToWideChar(
				CP_ACP, 0, ServiceName, (INT) Length + 1,
				CapturedName, KXADVAPI_SERVICE_NAME_CAPACITY);
			return Characters > 1;
		}
	}

	return FALSE;
}

STATIC VOID KxAdvapiForgetServiceHandle(IN SC_HANDLE Handle)
{
	ULONG Index;

	AcquireSRWLockExclusive(&KxAdvapiServiceTrackingLock);
	for (Index = 0; Index < KXADVAPI_SERVICE_TRACKING_CAPACITY; ++Index) {
		if (KxAdvapiServiceHandles[Index].Handle == Handle) {
			RtlZeroMemory(&KxAdvapiServiceHandles[Index],
				sizeof(KxAdvapiServiceHandles[Index]));
		}
	}
	ReleaseSRWLockExclusive(&KxAdvapiServiceTrackingLock);
}

STATIC BOOLEAN KxAdvapiTrackServiceHandle(
	IN SC_HANDLE Handle,
	IN ULONG Kind,
	IN DWORD DesiredAccess,
	IN PCWSTR ServiceName OPTIONAL)
{
	ULONG Index;
	ULONG FreeIndex;
	ULONG CharacterIndex;

	FreeIndex = KXADVAPI_SERVICE_TRACKING_CAPACITY;
	AcquireSRWLockExclusive(&KxAdvapiServiceTrackingLock);
	for (Index = 0; Index < KXADVAPI_SERVICE_TRACKING_CAPACITY; ++Index) {
		if (KxAdvapiServiceHandles[Index].Handle == Handle) {
			FreeIndex = Index;
			break;
		}
		if (KxAdvapiServiceHandles[Index].Handle == NULL &&
			FreeIndex == KXADVAPI_SERVICE_TRACKING_CAPACITY) {
			FreeIndex = Index;
		}
	}

	if (FreeIndex != KXADVAPI_SERVICE_TRACKING_CAPACITY) {
		KxAdvapiServiceHandles[FreeIndex].Handle = Handle;
		KxAdvapiServiceHandles[FreeIndex].Kind = Kind;
		KxAdvapiServiceHandles[FreeIndex].DesiredAccess = DesiredAccess;
		KxAdvapiServiceHandles[FreeIndex].ServiceName[0] = L'\0';
		if (ServiceName != NULL) {
			for (CharacterIndex = 0;
				CharacterIndex < KXADVAPI_SERVICE_NAME_CAPACITY;
				++CharacterIndex) {
				KxAdvapiServiceHandles[FreeIndex].ServiceName[CharacterIndex] =
					ServiceName[CharacterIndex];
				if (ServiceName[CharacterIndex] == L'\0') {
					break;
				}
			}
		}
	}
	ReleaseSRWLockExclusive(&KxAdvapiServiceTrackingLock);

	return FreeIndex != KXADVAPI_SERVICE_TRACKING_CAPACITY;
}

STATIC BOOLEAN KxAdvapiIsLocalServiceManager(IN SC_HANDLE Handle)
{
	ULONG Index;
	BOOLEAN IsLocal;

	IsLocal = FALSE;
	AcquireSRWLockShared(&KxAdvapiServiceTrackingLock);
	for (Index = 0; Index < KXADVAPI_SERVICE_TRACKING_CAPACITY; ++Index) {
		if (KxAdvapiServiceHandles[Index].Handle == Handle &&
			KxAdvapiServiceHandles[Index].Kind == KXADVAPI_SCM_HANDLE) {
			IsLocal = TRUE;
			break;
		}
	}
	ReleaseSRWLockShared(&KxAdvapiServiceTrackingLock);

	return IsLocal;
}

STATIC BOOLEAN KxAdvapiSnapshotStartServiceName(
	IN SC_HANDLE Handle,
	OUT PWSTR ServiceName)
{
	ULONG Index;
	ULONG CharacterIndex;
	BOOLEAN Found;

	Found = FALSE;
	AcquireSRWLockShared(&KxAdvapiServiceTrackingLock);
	for (Index = 0; Index < KXADVAPI_SERVICE_TRACKING_CAPACITY; ++Index) {
		if (KxAdvapiServiceHandles[Index].Handle != Handle ||
			KxAdvapiServiceHandles[Index].Kind != KXADVAPI_SERVICE_HANDLE ||
			!(KxAdvapiServiceHandles[Index].DesiredAccess &
				(SERVICE_START | GENERIC_EXECUTE | GENERIC_ALL))) {
			continue;
		}

		for (CharacterIndex = 0;
			CharacterIndex < KXADVAPI_SERVICE_NAME_CAPACITY;
			++CharacterIndex) {
			ServiceName[CharacterIndex] =
				KxAdvapiServiceHandles[Index].ServiceName[CharacterIndex];
			if (ServiceName[CharacterIndex] == L'\0') {
				break;
			}
		}
		Found = TRUE;
		break;
	}
	ReleaseSRWLockShared(&KxAdvapiServiceTrackingLock);

	return Found;
}

SC_HANDLE WINAPI Ext_OpenSCManagerW(
	IN LPCWSTR MachineName OPTIONAL,
	IN LPCWSTR DatabaseName OPTIONAL,
	IN DWORD DesiredAccess)
{
	SC_HANDLE Handle;
	DWORD ErrorCode;

	Handle = OpenSCManagerW(MachineName, DatabaseName, DesiredAccess);
	ErrorCode = GetLastError();
	if (Handle != NULL) {
		KxAdvapiForgetServiceHandle(Handle);
		if ((MachineName == NULL || MachineName[0] == L'\0') &&
			(DatabaseName == NULL ||
				KxAdvapiAsciiEqualIW(DatabaseName, SERVICES_ACTIVE_DATABASEW))) {
			KxAdvapiTrackServiceHandle(Handle, KXADVAPI_SCM_HANDLE, DesiredAccess, NULL);
		}
	}
	SetLastError(ErrorCode);
	return Handle;
}

SC_HANDLE WINAPI Ext_OpenSCManagerA(
	IN LPCSTR MachineName OPTIONAL,
	IN LPCSTR DatabaseName OPTIONAL,
	IN DWORD DesiredAccess)
{
	SC_HANDLE Handle;
	DWORD ErrorCode;

	Handle = OpenSCManagerA(MachineName, DatabaseName, DesiredAccess);
	ErrorCode = GetLastError();
	if (Handle != NULL) {
		KxAdvapiForgetServiceHandle(Handle);
		if ((MachineName == NULL || MachineName[0] == '\0') &&
			(DatabaseName == NULL ||
				KxAdvapiAsciiEqualIA(DatabaseName, SERVICES_ACTIVE_DATABASEA))) {
			KxAdvapiTrackServiceHandle(Handle, KXADVAPI_SCM_HANDLE, DesiredAccess, NULL);
		}
	}
	SetLastError(ErrorCode);
	return Handle;
}

SC_HANDLE WINAPI Ext_OpenServiceW(
	IN SC_HANDLE Manager,
	IN LPCWSTR ServiceName,
	IN DWORD DesiredAccess)
{
	SC_HANDLE Handle;
	DWORD ErrorCode;
	WCHAR CapturedName[KXADVAPI_SERVICE_NAME_CAPACITY];

	Handle = OpenServiceW(Manager, ServiceName, DesiredAccess);
	ErrorCode = GetLastError();
	if (Handle != NULL) {
		KxAdvapiForgetServiceHandle(Handle);
		if (KxAdvapiIsLocalServiceManager(Manager) &&
			KxAdvapiCaptureServiceNameW(ServiceName, CapturedName)) {
			KxAdvapiTrackServiceHandle(Handle, KXADVAPI_SERVICE_HANDLE,
				DesiredAccess, CapturedName);
		}
	}
	SetLastError(ErrorCode);
	return Handle;
}

SC_HANDLE WINAPI Ext_OpenServiceA(
	IN SC_HANDLE Manager,
	IN LPCSTR ServiceName,
	IN DWORD DesiredAccess)
{
	SC_HANDLE Handle;
	DWORD ErrorCode;
	WCHAR CapturedName[KXADVAPI_SERVICE_NAME_CAPACITY];

	Handle = OpenServiceA(Manager, ServiceName, DesiredAccess);
	ErrorCode = GetLastError();
	if (Handle != NULL) {
		KxAdvapiForgetServiceHandle(Handle);
		if (KxAdvapiIsLocalServiceManager(Manager) &&
			KxAdvapiCaptureServiceNameA(ServiceName, CapturedName)) {
			KxAdvapiTrackServiceHandle(Handle, KXADVAPI_SERVICE_HANDLE,
				DesiredAccess, CapturedName);
		}
	}
	SetLastError(ErrorCode);
	return Handle;
}

SC_HANDLE WINAPI Ext_CreateServiceW(
	IN SC_HANDLE Manager,
	IN LPCWSTR ServiceName,
	IN LPCWSTR DisplayName OPTIONAL,
	IN DWORD DesiredAccess,
	IN DWORD ServiceType,
	IN DWORD StartType,
	IN DWORD ErrorControl,
	IN LPCWSTR BinaryPathName OPTIONAL,
	IN LPCWSTR LoadOrderGroup OPTIONAL,
	OUT LPDWORD TagId OPTIONAL,
	IN LPCWSTR Dependencies OPTIONAL,
	IN LPCWSTR ServiceStartName OPTIONAL,
	IN LPCWSTR Password OPTIONAL)
{
	SC_HANDLE Handle;
	DWORD ErrorCode;
	WCHAR CapturedName[KXADVAPI_SERVICE_NAME_CAPACITY];

	Handle = CreateServiceW(Manager, ServiceName, DisplayName, DesiredAccess,
		ServiceType, StartType, ErrorControl, BinaryPathName, LoadOrderGroup,
		TagId, Dependencies, ServiceStartName, Password);
	ErrorCode = GetLastError();
	if (Handle != NULL) {
		KxAdvapiForgetServiceHandle(Handle);
		if (KxAdvapiIsLocalServiceManager(Manager) &&
			KxAdvapiCaptureServiceNameW(ServiceName, CapturedName) &&
			KxAdvapiTrackServiceHandle(Handle, KXADVAPI_SERVICE_HANDLE,
				DesiredAccess, CapturedName)) {
			KxAdvapiTryInheritService(CapturedName);
		}
	}
	SetLastError(ErrorCode);
	return Handle;
}

SC_HANDLE WINAPI Ext_CreateServiceA(
	IN SC_HANDLE Manager,
	IN LPCSTR ServiceName,
	IN LPCSTR DisplayName OPTIONAL,
	IN DWORD DesiredAccess,
	IN DWORD ServiceType,
	IN DWORD StartType,
	IN DWORD ErrorControl,
	IN LPCSTR BinaryPathName OPTIONAL,
	IN LPCSTR LoadOrderGroup OPTIONAL,
	OUT LPDWORD TagId OPTIONAL,
	IN LPCSTR Dependencies OPTIONAL,
	IN LPCSTR ServiceStartName OPTIONAL,
	IN LPCSTR Password OPTIONAL)
{
	SC_HANDLE Handle;
	DWORD ErrorCode;
	WCHAR CapturedName[KXADVAPI_SERVICE_NAME_CAPACITY];

	Handle = CreateServiceA(Manager, ServiceName, DisplayName, DesiredAccess,
		ServiceType, StartType, ErrorControl, BinaryPathName, LoadOrderGroup,
		TagId, Dependencies, ServiceStartName, Password);
	ErrorCode = GetLastError();
	if (Handle != NULL) {
		KxAdvapiForgetServiceHandle(Handle);
		if (KxAdvapiIsLocalServiceManager(Manager) &&
			KxAdvapiCaptureServiceNameA(ServiceName, CapturedName) &&
			KxAdvapiTrackServiceHandle(Handle, KXADVAPI_SERVICE_HANDLE,
				DesiredAccess, CapturedName)) {
			KxAdvapiTryInheritService(CapturedName);
		}
	}
	SetLastError(ErrorCode);
	return Handle;
}

BOOL WINAPI Ext_CloseServiceHandle(IN SC_HANDLE Handle)
{
	BOOL Result;
	DWORD ErrorCode;

	Result = CloseServiceHandle(Handle);
	ErrorCode = GetLastError();
	if (Result) {
		KxAdvapiForgetServiceHandle(Handle);
	}
	SetLastError(ErrorCode);
	return Result;
}

BOOL WINAPI Ext_StartServiceW(
	IN SC_HANDLE Handle,
	IN DWORD ArgumentCount,
	IN LPCWSTR *Arguments OPTIONAL)
{
	BOOL Result;
	DWORD EntryError;
	WCHAR ServiceName[KXADVAPI_SERVICE_NAME_CAPACITY];

	EntryError = GetLastError();
	if (KxAdvapiSnapshotStartServiceName(Handle, ServiceName)) {
		KxAdvapiTryInheritService(ServiceName);
	}
	SetLastError(EntryError);
	Result = StartServiceW(Handle, ArgumentCount, Arguments);
	return Result;
}

BOOL WINAPI Ext_StartServiceA(
	IN SC_HANDLE Handle,
	IN DWORD ArgumentCount,
	IN LPCSTR *Arguments OPTIONAL)
{
	BOOL Result;
	DWORD EntryError;
	WCHAR ServiceName[KXADVAPI_SERVICE_NAME_CAPACITY];

	EntryError = GetLastError();
	if (KxAdvapiSnapshotStartServiceName(Handle, ServiceName)) {
		KxAdvapiTryInheritService(ServiceName);
	}
	SetLastError(EntryError);
	Result = StartServiceA(Handle, ArgumentCount, Arguments);
	return Result;
}

#ifdef KEX_SERVICE_TEST
VOID KxAdvapiResetServiceTrackingForTest(VOID)
{
	AcquireSRWLockExclusive(&KxAdvapiServiceTrackingLock);
	RtlZeroMemory(KxAdvapiServiceHandles, sizeof(KxAdvapiServiceHandles));
	ReleaseSRWLockExclusive(&KxAdvapiServiceTrackingLock);
}
#endif
