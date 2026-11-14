#include "buildcfg.h"
#include "kxbasep.h"

KXBASEAPI BOOL WINAPI GetProcessInformation(
	IN	HANDLE						ProcessHandle,
	IN	PROCESS_INFORMATION_CLASS	ProcessInformationClass,
	OUT	PVOID						ProcessInformation,
	IN	ULONG						ProcessInformationSize)
{
	NTSTATUS Status;
	PAGE_PRIORITY_INFORMATION PagePriority;
	ULONG LastError;

	if (ProcessInformationClass != ProcessMemoryPriority) {
		BaseSetLastNTError((ULONG) ProcessInformationClass >= ProcessInformationClassMax ?
			STATUS_INVALID_PARAMETER : STATUS_NOT_SUPPORTED);
		return FALSE;
	}

	if (ProcessInformationSize != sizeof(PagePriority)) {
		BaseSetLastNTError(STATUS_INFO_LENGTH_MISMATCH);
		return FALSE;
	}

	LastError = GetLastError();
	Status = NtQueryInformationProcess(ProcessHandle, ProcessPagePriority,
		&PagePriority, sizeof(PagePriority), NULL);
	if (!NT_SUCCESS(Status)) {
		BaseSetLastNTError(Status);
		return FALSE;
	}

	// Publish only a completed kernel query. Native Windows returns NOACCESS for
	// an inaccessible output buffer rather than exposing an access violation.
	try {
		*(PPAGE_PRIORITY_INFORMATION) ProcessInformation = PagePriority;
	} except(EXCEPTION_EXECUTE_HANDLER) {
		BaseSetLastNTError(GetExceptionCode());
		return FALSE;
	}

	SetLastError(LastError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI SetProcessInformation(
	IN	HANDLE						ProcessHandle,
	IN	PROCESS_INFORMATION_CLASS	ProcessInformationClass,
	IN	PVOID						ProcessInformation,
	IN	ULONG						ProcessInformationSize)
{
	NTSTATUS Status;
	PAGE_PRIORITY_INFORMATION PagePriority;
	ULONG LastError;

	if (ProcessInformationClass != ProcessMemoryPriority) {
		BaseSetLastNTError((ULONG) ProcessInformationClass >= ProcessInformationClassMax ?
			STATUS_INVALID_PARAMETER : STATUS_NOT_SUPPORTED);
		return FALSE;
	}

	if (ProcessInformationSize != sizeof(PagePriority)) {
		BaseSetLastNTError(STATUS_INFO_LENGTH_MISMATCH);
		return FALSE;
	}

	LastError = GetLastError();
	try {
		PagePriority = *(PPAGE_PRIORITY_INFORMATION) ProcessInformation;
	} except(EXCEPTION_EXECUTE_HANDLER) {
		BaseSetLastNTError(GetExceptionCode());
		return FALSE;
	}

	// Public MEMORY_PRIORITY_INFORMATION permits priorities 1 through 5.
	// Use the real Win7 memory-manager policy, not the CPU priority or a cache.
	if (PagePriority.PagePriority < 1 || PagePriority.PagePriority > 5) {
		BaseSetLastNTError(STATUS_INVALID_PARAMETER);
		return FALSE;
	}

	Status = NtSetInformationProcess(ProcessHandle, ProcessPagePriority,
		&PagePriority, sizeof(PagePriority));
	if (!NT_SUCCESS(Status)) {
		BaseSetLastNTError(Status);
		return FALSE;
	}

	SetLastError(LastError);
	return TRUE;
}

//
// CPU set APIs live in cpuset.c. They are implemented with the real processor
// topology and Windows 7 affinity instead of the previous success-only stubs.
//

KXBASEAPI BOOL WINAPI SetProcessMitigationPolicy(
	IN	PROCESS_MITIGATION_POLICY	MitigationPolicy,
	IN	PVOID						Buffer,
	IN	SIZE_T						BufferCb)
{
	//
	// Note that Windows 7 has SetProcessDEPPolicy but it doesn't do anything
	// for x64.
	//

	if (KexRtlCurrentProcessBitness() == 32 && MitigationPolicy == ProcessDEPPolicy) {
		PPROCESS_MITIGATION_DEP_POLICY DepPolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_DEP_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		DepPolicy = (PPROCESS_MITIGATION_DEP_POLICY) Buffer;

		if (DepPolicy->Flags.ReservedFlags) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}

		return SetProcessDEPPolicy(DepPolicy->Flags.AsUlong);
	} else {
		KexLogWarningEvent(
			L"SetProcessMitigationPolicy called with unsupported MitigationPolicy value %d",
			MitigationPolicy);

		// Fall through and pretend we succeeded.
	}

	return TRUE;
}

KXBASEAPI BOOL WINAPI GetProcessMitigationPolicy(
	IN	HANDLE						ProcessHandle,
	IN	PROCESS_MITIGATION_POLICY	MitigationPolicy,
	OUT	PVOID						Buffer,
	IN	SIZE_T						BufferCb)
{
	if (MitigationPolicy == ProcessMitigationOptionsMask) {
		PULONGLONG Mask;

		//
		// Buffer is a pointer to either one or two ULONGLONGs.
		// The first one contains PROCESS_CREATION_MITIGATION_POLICY_*.
		// The second one if present contains PROCESS_CREATION_MITIGATION_POLICY2_*
		//

		if (BufferCb != 8 && BufferCb != 16) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}

		Mask = (PULONGLONG) Buffer;

		Mask[0] = PROCESS_CREATION_MITIGATION_POLICY_VALID_MASK;

		if (BufferCb > 8) {
			Mask[1] = 0;
		}

		return TRUE;
	} else if (MitigationPolicy == ProcessSystemCallDisablePolicy) {
		PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY SystemCallPolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}

		SystemCallPolicy = (PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY) Buffer;

		//
		// Always set this to 0 because Win7 does not support disabling win32k or
		// any other syscalls.
		//

		SystemCallPolicy->Flags = 0;

		return TRUE;
	} else if (MitigationPolicy == ProcessDEPPolicy) {
		BOOL Success;
		PPROCESS_MITIGATION_DEP_POLICY DepPolicy;
		ULONG DepFlags;
		BOOL Permanent;

		if (BufferCb != sizeof(PROCESS_MITIGATION_DEP_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		DepPolicy = (PPROCESS_MITIGATION_DEP_POLICY) Buffer;

		Success = GetProcessDEPPolicy(
			ProcessHandle,
			&DepFlags,
			&Permanent);

		if (!Success) {
			return FALSE;
		}

		DepPolicy->Flags.AsUlong = DepFlags;
		DepPolicy->Permanent = !!Permanent;
		return TRUE;
	} else if (MitigationPolicy == ProcessSystemCallDisablePolicy) {
		PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY SystemCallDisablePolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		SystemCallDisablePolicy = (PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY) Buffer;

		SystemCallDisablePolicy->DisallowWin32kSystemCalls = 0;
		SystemCallDisablePolicy->AuditDisallowWin32kSystemCalls = 0;
		SystemCallDisablePolicy->DisallowFsctlSystemCalls = 0;
		SystemCallDisablePolicy->AuditDisallowFsctlSystemCalls = 0;

		return TRUE;
	} else if (MitigationPolicy == ProcessSignaturePolicy) {
		PPROCESS_MITIGATION_BINARY_SIGNATURE_POLICY SignaturePolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		SignaturePolicy = (PPROCESS_MITIGATION_BINARY_SIGNATURE_POLICY) Buffer;

		SignaturePolicy->AuditMicrosoftSignedOnly = 0;
		SignaturePolicy->AuditStoreSignedOnly = 0;
		SignaturePolicy->MicrosoftSignedOnly = 0;
		SignaturePolicy->MitigationOptIn = 0;
		SignaturePolicy->StoreSignedOnly = 0;

		return TRUE;
	} else {
		KexLogWarningEvent(
			L"GetProcessMitigationPolicy called with unsupported MitigationPolicy value %d",
			MitigationPolicy);
	}

	BaseSetLastNTError(STATUS_NOT_SUPPORTED);
	return FALSE;
}

KXBASEAPI BOOL WINAPI IsProcessCritical(
	IN	HANDLE	ProcessHandle,
	OUT	PBOOL	Critical)
{
	ULONG BreakOnTermination = 0;
	NTSTATUS Status = NtQueryInformationProcess(ProcessHandle, ProcessBreakOnTermination,  &BreakOnTermination, sizeof(BreakOnTermination), NULL);
	if (NT_SUCCESS(Status)) {
		*Critical = BreakOnTermination != 0;
		return TRUE;
	}
	return FALSE;
}

KXBASEAPI BOOL WINAPI Ext_IsProcessInJob(
	IN	HANDLE	ProcessHandle,
	IN	HANDLE	JobHandle,
	OUT	PBOOL	IsInJob)
{
	//
	// APPSPECIFICHACK: Make Chromium non-official builds not meddle with the IAT.
	// See sandbox\policy\win\sandbox_win.cc SandboxWin::InitBrokerServices.
	// This function should only be called in one place.
	//

	if ((KexData->Flags & KEXDATA_FLAG_CHROMIUM) &&
		AshModuleBaseNameIs(ReturnAddress(), L"chrome.dll")) {

		ASSERT (ProcessHandle == NtCurrentProcess());
		ASSERT (JobHandle == NULL);
		ASSERT (IsInJob != NULL);

		KexLogDebugEvent(L"Returning fake IsProcessInJob return value for Chrome compatibility");

		if (ProcessHandle == NtCurrentProcess() &&
			JobHandle == NULL &&
			IsInJob != NULL) {

			*IsInJob = TRUE;
			return TRUE;
		}
	}

	return IsProcessInJob(ProcessHandle, JobHandle, IsInJob);
}

KXBASEAPI BOOL WINAPI Ext_UpdateProcThreadAttribute(
	IN OUT	PPROC_THREAD_ATTRIBUTE_LIST	AttributeList,
	IN		ULONG						Flags,
	IN		ULONG_PTR					Attribute,
	IN		PVOID						Value,
	IN		SIZE_T						Size,
	OUT		PVOID						PreviousValue OPTIONAL,
	OUT		PSIZE_T						ReturnSize OPTIONAL)
{
	BOOLEAN Success;
	BOOLEAN AlreadyTriedAgain;
	ULONG MitigationPolicy;

	AlreadyTriedAgain = FALSE;

TryAgain:
	Success = UpdateProcThreadAttribute(
		AttributeList,
		Flags,
		Attribute,
		Value,
		Size,
		PreviousValue,
		ReturnSize);

	if (!Success) {
		ULONG LastError;

		// Save last-error code so we can restore it. The code below might modify it
		// by accident.
		LastError = GetLastError();

		if (AlreadyTriedAgain) {
			KexLogWarningEvent(
				L"UpdateProcThreadAttribute failed despite modifying parameters.\r\n\r\n"
				L"Win32 error: (%d) %s",
				LastError, Win32ErrorAsString(LastError));

			ASSERT (FALSE);
		} else {
			if (LastError == ERROR_NOT_SUPPORTED) {
				switch (Attribute) {
				case PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY:
					//
					// Edit the mitigation policy and go try again.
					//

					if (!AlreadyTriedAgain) {
						KexLogDebugEvent(L"Unsupported mitigation policies specified. Stripping.");

						if (Size > sizeof(ULONG)) {
							ASSERT (Size == sizeof(ULONGLONG) || Size == 2 * sizeof(ULONGLONG));
							Size = sizeof(ULONG);
						}

						MitigationPolicy = (*(PULONG) Value) & PROCESS_CREATION_MITIGATION_POLICY_VALID_MASK;
						Value = &MitigationPolicy;
				
						AlreadyTriedAgain = TRUE;
						goto TryAgain;
					}
				
					break;
				default:
					// Just pretend we succeeded and hope nothing bad happens.
					// Most of the extra mitigation policies don't really do anything anyway,
					// so it should be fine to just pretend it succeeded.
					Success = TRUE;
					LastError = ERROR_SUCCESS;
					break;
				}
			}
		}

		SetLastError(LastError);
	}

	return Success;
}
