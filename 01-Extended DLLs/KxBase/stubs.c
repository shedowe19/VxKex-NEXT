///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     stubs.c
//
// Abstract:
//
//     Forwarder stubs that do nothing except for calling the original function.
//     These exist because some stupid software such as Chromium is not
//     compatible with export forwarders.
//
// Author:
//
//     vxiiduu (09-Mar-2024)
//
// Environment:
//
//     Win32
//
// Revision History:
//
//     vxiiduu              09-Mar-2024  Initial creation.
//
///////////////////////////////////////////////////////////////////////////////

#include "buildcfg.h"
#include "kxbasep.h"

KXBASEAPI BOOL WINAPI Stub_DuplicateHandle(
	IN	HANDLE		SourceProcessHandle,
	IN	HANDLE		SourceHandle,
	IN	HANDLE		TargetProcessHandle,
	OUT	PHANDLE		TargetHandle,
	IN	ACCESS_MASK	DesiredAccess,
	IN	BOOL		Inherit,
	IN	ULONG		Options)
{
	return DuplicateHandle(
		SourceProcessHandle,
		SourceHandle,
		TargetProcessHandle,
		TargetHandle,
		DesiredAccess,
		Inherit,
		Options);
}

KXBASEAPI FARPROC WINAPI Stub_GetProcAddress(
	IN	HMODULE		ModuleHandle,
	IN	PCSTR		ProcedureName)
{
	FARPROC Procedure;
	ULONG OriginalError;
	ULONG Index;
	HMODULE Provider;
	static const CHAR ExpectedProcedureName[] = "RtlGetDeviceFamilyInfoEnum";

	Procedure = GetProcAddress(
		ModuleHandle,
		ProcedureName);

	if (Procedure) {
		return Procedure;
	}

	OriginalError = GetLastError();

	if (OriginalError != ERROR_PROC_NOT_FOUND ||
		(ULONG_PTR) ProcedureName <= 0xFFFF) {

		return NULL;
	}

	for (Index = 0; Index < sizeof(ExpectedProcedureName); ++Index) {
		if (ProcedureName[Index] != ExpectedProcedureName[Index]) {
			return NULL;
		}
	}

	// Keep the real NTDLL handle for Chromium's native syscall inspection.
	// Supply only this missing procedure through the already loaded provider.
	if (ModuleHandle == GetModuleHandleW(L"ntdll.dll")) {
		Provider = GetModuleHandleW(L"KexDll.dll");

		if (Provider) {
			Procedure = GetProcAddress(
				Provider,
				"KexRtlGetDeviceFamilyInfoEnum");
		}
	}

	SetLastError(OriginalError);
	return Procedure;
}
