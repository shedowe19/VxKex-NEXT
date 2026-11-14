#include "buildcfg.h"
#include "kexdllp.h"

// ProcessHandleTable (58) returns an array of ULONG handle values, including
// in 64-bit callers. It has no count/header. The native Windows 11 reference
// accepts partial arrays and reports the bytes actually written.
typedef struct _KEX_SYSTEM_HANDLE_ENTRY_EX {
	PVOID Object;
	ULONG_PTR UniqueProcessId;
	ULONG_PTR HandleValue;
	ULONG GrantedAccess;
	USHORT CreatorBackTraceIndex;
	USHORT ObjectTypeIndex;
	ULONG HandleAttributes;
	ULONG Reserved;
} KEX_SYSTEM_HANDLE_ENTRY_EX;

typedef struct _KEX_SYSTEM_HANDLE_INFORMATION_EX {
	ULONG_PTR NumberOfHandles;
	ULONG_PTR Reserved;
	KEX_SYSTEM_HANDLE_ENTRY_EX Handles[1];
} KEX_SYSTEM_HANDLE_INFORMATION_EX;

// Match the native probing order without changing the caller's data. Like
// ProbeForWrite, this touches every covered page and the last byte; the final
// copy is still protected because another thread can change a mapping later.
static NTSTATUS KexpProbeHandleTableBuffer(PVOID Buffer, SIZE_T Length)
{
	ULONG_PTR Address = (ULONG_PTR) Buffer;
	ULONG_PTR LastAddress;
	ULONG_PTR ProbeAddress;
	volatile BYTE *Byte;

	if (!Length) {
		return STATUS_SUCCESS;
	}

	if (!Address || Length - 1 > (ULONG_PTR) -1 - Address) {
		return STATUS_ACCESS_VIOLATION;
	}

	LastAddress = Address + Length - 1;
	__try {
		Byte = (volatile BYTE *) Address;
		*Byte = *Byte;
		ProbeAddress = Address | (PAGE_SIZE - 1);
		while (ProbeAddress < LastAddress) {
			++ProbeAddress;
			Byte = (volatile BYTE *) ProbeAddress;
			*Byte = *Byte;
			ProbeAddress |= PAGE_SIZE - 1;
		}
		Byte = (volatile BYTE *) LastAddress;
		*Byte = *Byte;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return GetExceptionCode();
	}

	return STATUS_SUCCESS;
}

// In-place heapsort avoids a second allocation and has bounded O(n log n)
// running time even for a process with a large handle table.
static VOID KexpSortHandleValues(PULONG Values, ULONG_PTR Count)
{
	ULONG_PTR Start;
	ULONG_PTR End;
	ULONG_PTR Root;
	ULONG_PTR Child;
	ULONG Value;

	if (Count < 2) {
		return;
	}

	Start = Count / 2;
	End = Count;
	while (End > 1) {
		if (Start) {
			Value = Values[--Start];
		} else {
			Value = Values[--End];
			Values[End] = Values[0];
		}
		Root = Start;
		while (Root < End / 2) {
			Child = Root * 2 + 1;
			if (Child + 1 < End && Values[Child] < Values[Child + 1]) {
				++Child;
			}
			if (Value >= Values[Child]) {
				break;
			}
			Values[Root] = Values[Child];
			Root = Child;
		}
		Values[Root] = Value;
	}
}

static NTSTATUS KexpQueryProcessHandleTable(
	HANDLE ProcessHandle,
	PVOID ProcessInformation,
	ULONG ProcessInformationLength,
	PULONG ReturnLength)
{
	NTSTATUS Status;
	HANDLE StableProcessHandle = NULL;
	PROCESS_BASIC_INFORMATION BasicInformation;
	PROCESS_BASIC_INFORMATION FinalBasicInformation;
	KERNEL_USER_TIMES ProcessTimesInformation;
	OBJECT_BASIC_INFORMATION ObjectInformation;
	KEX_SYSTEM_HANDLE_INFORMATION_EX *Table = NULL;
	ULONG BufferSize = 0x10000;
	ULONG RequiredSize = 0;
	ULONG Attempt;
	ULONG BytesWritten;
	ULONG_PTR Index;
	ULONG_PTR HandleCount = 0;
	ULONG_PTR SystemHandleCount;
	ULONG_PTR Capacity;
	ULONG_PTR HeaderSize = FIELD_OFFSET(KEX_SYSTEM_HANDLE_INFORMATION_EX, Handles);
	BOOLEAN QueryingSelf;
	PULONG Values;
	ULONG SavedLastError = RtlGetLastWin32Error();

	// Modern NT checks alignment for a nonempty output and probes ReturnLength
	// before rejecting an undersized class-58 buffer. Win7's WOW64 unsupported-
	// class path does not do this probing, so the fallback must reproduce it.
	if (ProcessInformationLength &&
		((ULONG_PTR) ProcessInformation & (sizeof(ULONG) - 1))) {
		return STATUS_DATATYPE_MISALIGNMENT;
	}
	if (ReturnLength) {
		Status = KexpProbeHandleTableBuffer(ReturnLength, sizeof(*ReturnLength));
		if (!NT_SUCCESS(Status)) {
			return Status;
		}
	}
	if (ProcessInformationLength < sizeof(ULONG)) {
		return STATUS_INFO_LENGTH_MISMATCH;
	}
	Status = KexpProbeHandleTableBuffer(ProcessInformation, ProcessInformationLength);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	// Hold a duplicate of precisely the supplied handle and its granted access.
	// Do not reopen the PID, elevate privileges, or grant the caller new rights.
	Status = NtDuplicateObject(NtCurrentProcess(), ProcessHandle, NtCurrentProcess(),
		&StableProcessHandle, 0, 0, DUPLICATE_SAME_ACCESS);
	if (!NT_SUCCESS(Status)) {
		goto Cleanup;
	}
	Status = NtQueryInformationProcess(StableProcessHandle, ProcessBasicInformation,
		&BasicInformation, sizeof(BasicInformation), NULL);
	if (!NT_SUCCESS(Status)) {
		goto Cleanup;
	}
	Status = NtQueryObject(StableProcessHandle, ObjectBasicInformation,
		&ObjectInformation, sizeof(ObjectInformation), NULL);
	if (!NT_SUCCESS(Status)) {
		goto Cleanup;
	}
	if ((ObjectInformation.GrantedAccess &
		(PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE)) !=
		(PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE)) {
		Status = STATUS_ACCESS_DENIED;
		goto Cleanup;
	}
	Status = NtQueryInformationProcess(StableProcessHandle, ProcessTimes,
		&ProcessTimesInformation, sizeof(ProcessTimesInformation), NULL);
	if (!NT_SUCCESS(Status)) {
		goto Cleanup;
	}
	// A terminated process may itself have exit code STATUS_PENDING (259).
	// ExitStatus alone is therefore insufficient to guard against PID reuse.
	if (BasicInformation.ExitStatus != STATUS_PENDING || ProcessTimesInformation.ExitTime) {
		Status = STATUS_PROCESS_IS_TERMINATING;
		goto ReturnTerminating;
	}
	QueryingSelf = BasicInformation.UniqueProcessId ==
		(ULONG_PTR) NtCurrentTeb()->ClientId.UniqueProcess;

	// This is a real kernel snapshot. The bound/retry limit avoids unbounded
	// allocations if system handle counts grow while querying. Backend errors
	// remain errors; in particular, no debug privilege is enabled on failure.
	for (Attempt = 0; Attempt < 12; ++Attempt) {
		Table = (KEX_SYSTEM_HANDLE_INFORMATION_EX *) SafeAlloc(BYTE, BufferSize);
		if (!Table) {
			Status = STATUS_NO_MEMORY;
			goto Cleanup;
		}
		Status = NtQuerySystemInformation(SystemExtendedHandleInformation,
			Table, BufferSize, &RequiredSize);
		if (Status != STATUS_INFO_LENGTH_MISMATCH) {
			break;
		}
		SafeFree(Table);
		if (BufferSize >= 0x4000000 || RequiredSize > 0x4000000) {
			Status = STATUS_INSUFFICIENT_RESOURCES;
			goto Cleanup;
		}
		BufferSize = max(BufferSize * 2, RequiredSize);
	}
	if (!NT_SUCCESS(Status)) {
		goto Cleanup;
	}
	if (!Table || RequiredSize < HeaderSize || RequiredSize > BufferSize ||
		Table->NumberOfHandles > (RequiredSize - HeaderSize) / sizeof(Table->Handles[0])) {
		Status = STATUS_INFO_LENGTH_MISMATCH;
		goto Cleanup;
	}

	// Compact only this process's numeric handle values into the beginning of
	// our private buffer. The write cursor stays behind all unread records.
	Values = (PULONG) Table;
	Capacity = ProcessInformationLength / sizeof(ULONG);
	SystemHandleCount = Table->NumberOfHandles;
	for (Index = 0; Index < SystemHandleCount; ++Index) {
		KEX_SYSTEM_HANDLE_ENTRY_EX *Entry = &Table->Handles[Index];
		if (Entry->UniqueProcessId != BasicInformation.UniqueProcessId ||
			(QueryingSelf && Entry->HandleValue == (ULONG_PTR) StableProcessHandle)) {
			continue;
		}
		if (Entry->HandleValue > (ULONG) -1) {
			Status = STATUS_INTEGER_OVERFLOW;
			goto Cleanup;
		}
		Values[HandleCount++] = (ULONG) Entry->HandleValue;
	}
	KexpSortHandleValues(Values, HandleCount);

	// A PID can be reused after exit. Only return the snapshot if the exact
	// referenced process was still live after it; do not expose a later process.
	Status = NtQueryInformationProcess(StableProcessHandle, ProcessBasicInformation,
		&FinalBasicInformation, sizeof(FinalBasicInformation), NULL);
	if (!NT_SUCCESS(Status)) {
		goto Cleanup;
	}
	Status = NtQueryInformationProcess(StableProcessHandle, ProcessTimes,
		&ProcessTimesInformation, sizeof(ProcessTimesInformation), NULL);
	if (!NT_SUCCESS(Status)) {
		goto Cleanup;
	}
	if (FinalBasicInformation.ExitStatus != STATUS_PENDING || ProcessTimesInformation.ExitTime) {
		Status = STATUS_PROCESS_IS_TERMINATING;
		goto ReturnTerminating;
	}
	BytesWritten = (ULONG) (min(HandleCount, Capacity) * sizeof(ULONG));
	__try {
		RtlCopyMemory(ProcessInformation, Values, BytesWritten);
		if (ReturnLength) {
			*ReturnLength = BytesWritten;
		}
		Status = STATUS_SUCCESS;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Status = GetExceptionCode();
	}
	goto Cleanup;

ReturnTerminating:
	__try {
		if (ReturnLength) {
			*ReturnLength = 0;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Status = GetExceptionCode();
	}

Cleanup:
	if (Table) {
		SafeFree(Table);
	}
	if (StableProcessHandle) {
		NtClose(StableProcessHandle);
	}
	RtlSetLastWin32Error(SavedLastError);
	return Status;
}

NTSTATUS NTAPI Ext_NtQueryInformationProcess(
	IN	HANDLE				ProcessHandle,
	IN	PROCESSINFOCLASS	ProcessInformationClass,
	OUT	PVOID				ProcessInformation,
	IN	ULONG				ProcessInformationLength,
	OUT	PULONG				ReturnLength OPTIONAL)
{
	NTSTATUS Status = NtQueryInformationProcess(
		ProcessHandle,
		ProcessInformationClass,
		ProcessInformation,
		ProcessInformationLength,
		ReturnLength);

	// Preserve a real native implementation, all other classes, and failures
	// unrelated to an unsupported information class. Class 52 remains native:
	// returning synthetic mitigation flags cannot provide kernel enforcement.
	if (Status == STATUS_INVALID_INFO_CLASS && (ULONG) ProcessInformationClass == 58) {
		return KexpQueryProcessHandleTable(ProcessHandle, ProcessInformation,
			ProcessInformationLength, ReturnLength);
	}
	return Status;
}
