#include "buildcfg.h"
#include "kxbasep.h"

//<CPU_SET_TEST_BODY>
//
//
// CPU set support for Windows 7.
//
// Windows 7 has no CPU set scheduler objects. This module provides the real
// system topology through GetSystemCpuSetInformation and implements the
// process/thread CPU set APIs with Windows 7's affinity mechanism:
//
//   - GetSystemCpuSetInformation enumerates the actual logical processors from
//     GetLogicalProcessorInformationEx. IDs are stable: (Group << 16) | index.
//   - Setting default/selected CPU sets restricts the process or thread with
//     SetProcessAffinityMask/SetThreadAffinityMask. Windows 7 applies affinity
//     to all existing threads too, so this is a hard restriction rather than
//     the soft scheduling preference of Windows 10. The previous affinity is
//     recorded and restored when the set is cleared.
//   - An empty set means "no restriction" and restores the recorded affinity.
//   - LastError is preserved on success, matching the observed native APIs.
//
// Cache and NUMA indexes are best-effort topology metadata. Windows 7's
// scheduler does not expose per-cache/NUMA placement control.
//

#define KX_MAX_CPU_SETS 256
#define KX_MAX_THREAD_SETS 64

typedef struct _KX_CPU_SET_ENTRY {
	ULONG	Id;
	USHORT	Group;
	UCHAR	LogicalProcessorIndex;
	UCHAR	CoreIndex;
	UCHAR	LastLevelCacheIndex;
	UCHAR	NumaNodeIndex;
	UCHAR	EfficiencyClass;
} KX_CPU_SET_ENTRY;

#pragma warning(push)
#pragma warning(disable: 4201)
typedef struct _KX_SYSTEM_CPU_SET_INFORMATION {
	DWORD	Size;
	DWORD	Type;
	struct {
		DWORD	Id;
		WORD	Group;
		BYTE	LogicalProcessorIndex;
		BYTE	CoreIndex;
		BYTE	LastLevelCacheIndex;
		BYTE	NumaNodeIndex;
		BYTE	EfficiencyClass;
		BYTE	AllFlags;
		union {
			DWORD	Reserved;
			BYTE	SchedulingClass;
		};
		DWORD64	AllocationTag;
	} CpuSet;
} KX_SYSTEM_CPU_SET_INFORMATION, *PKX_SYSTEM_CPU_SET_INFORMATION;
#pragma warning(pop)

typedef struct _KX_THREAD_CPU_SET {
	DWORD		ThreadId;
	ULONGLONG	CreationTime;
	KAFFINITY	Selected;
	KAFFINITY	Baseline;
	BOOLEAN		Valid;
} KX_THREAD_CPU_SET, *PKX_THREAD_CPU_SET;

STATIC SRWLOCK CpuSetLock = SRWLOCK_INIT;
STATIC KX_CPU_SET_ENTRY CpuSets[KX_MAX_CPU_SETS];
STATIC ULONG CpuSetCount;
STATIC BOOLEAN CpuSetTopologyReady;

STATIC SRWLOCK ProcessSetLock = SRWLOCK_INIT;
STATIC KAFFINITY ProcessDefaultMask;
STATIC KAFFINITY ProcessBaselineMask;
STATIC BOOLEAN ProcessDefaultValid;

STATIC SRWLOCK ThreadSetLock = SRWLOCK_INIT;
STATIC KX_THREAD_CPU_SET ThreadSets[KX_MAX_THREAD_SETS];

STATIC VOID KxBuildCpuSetArray(VOID)
{
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info = NULL;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Entry;
	DWORD Length = 0, Offset;
	ULONG Group, Bit;
	ULONG CoreIndex[64] = {0};
	SYSTEM_INFO SystemInfo;

	AcquireSRWLockExclusive(&CpuSetLock);

	if (CpuSetTopologyReady) {
		ReleaseSRWLockExclusive(&CpuSetLock);
		return;
	}

	if (!GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &Length) &&
		GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
		Length != 0) {
		Info = HeapAlloc(GetProcessHeap(), 0, Length);
	}

	if (Info && GetLogicalProcessorInformationEx(RelationProcessorCore, Info, &Length)) {
		for (Offset = 0; Offset < Length; ) {
			Entry = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) ((PBYTE) Info + Offset);

			if (Entry->Size == 0 || Offset + Entry->Size > Length) {
				break;
			}

			if (Entry->Relationship == RelationProcessorCore) {
				for (Group = 0; Group < Entry->Processor.GroupCount; ++Group) {
					if (Entry->Processor.GroupMask[Group].Group >= 64) {
						continue;
					}

					for (Bit = 0; Bit < 8 * sizeof(KAFFINITY); ++Bit) {
						if (!(Entry->Processor.GroupMask[Group].Mask & ((KAFFINITY) 1 << Bit))) {
							continue;
						}

						if (CpuSetCount >= KX_MAX_CPU_SETS) {
							break;
						}

						CpuSets[CpuSetCount].Id =
							((ULONG) Entry->Processor.GroupMask[Group].Group << 16) | Bit;
						CpuSets[CpuSetCount].Group =
							(USHORT) Entry->Processor.GroupMask[Group].Group;
						CpuSets[CpuSetCount].LogicalProcessorIndex = (UCHAR) Bit;
						CpuSets[CpuSetCount].CoreIndex =
							(UCHAR) CoreIndex[Entry->Processor.GroupMask[Group].Group];
						CpuSets[CpuSetCount].LastLevelCacheIndex = 0;
						CpuSets[CpuSetCount].NumaNodeIndex = 0;
						CpuSets[CpuSetCount].EfficiencyClass =
							((PBYTE) &Entry->Processor)[1]; // Reserved on Win7, EfficiencyClass on newer
						++CpuSetCount;
					}

					++CoreIndex[Entry->Processor.GroupMask[Group].Group];
				}
			}

			Offset += Entry->Size;
		}
	}

	if (Info) {
		HeapFree(GetProcessHeap(), 0, Info);
	}

	if (CpuSetCount == 0) {
		// Conservative fallback: one set per processor in the active group.
		GetSystemInfo(&SystemInfo);

		for (Bit = 0; Bit < SystemInfo.dwNumberOfProcessors && Bit < KX_MAX_CPU_SETS; ++Bit) {
			CpuSets[CpuSetCount].Id = Bit;
			CpuSets[CpuSetCount].Group = 0;
			CpuSets[CpuSetCount].LogicalProcessorIndex = (UCHAR) Bit;
			CpuSets[CpuSetCount].CoreIndex = (UCHAR) Bit;
			CpuSets[CpuSetCount].LastLevelCacheIndex = 0;
			CpuSets[CpuSetCount].NumaNodeIndex = 0;
			CpuSets[CpuSetCount].EfficiencyClass = 0;
			++CpuSetCount;
		}
	}

	CpuSetTopologyReady = TRUE;
	ReleaseSRWLockExclusive(&CpuSetLock);
}

STATIC ULONG KxCpuSetCount(VOID)
{
	KxBuildCpuSetArray();
	return CpuSetCount;
}

STATIC BOOLEAN KxFindCpuSet(IN ULONG Id, OUT KX_CPU_SET_ENTRY *Entry)
{
	ULONG Index;

	KxBuildCpuSetArray();

	for (Index = 0; Index < CpuSetCount; ++Index) {
		if (CpuSets[Index].Id == Id) {
			if (Entry) {
				*Entry = CpuSets[Index];
			}
			return TRUE;
		}
	}

	return FALSE;
}

STATIC BOOLEAN KxMaskFromCpuSetIds(
	IN	PULONG	CpuSetIds,
	IN	ULONG	NumberOfCpuSetIds,
	OUT	PKAFFINITY Mask)
{
	ULONG Index;
	KX_CPU_SET_ENTRY Entry;
	KAFFINITY Result = 0;

	if (NumberOfCpuSetIds != 0 && CpuSetIds == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	for (Index = 0; Index < NumberOfCpuSetIds; ++Index) {
		if (!KxFindCpuSet(CpuSetIds[Index], &Entry)) {
			SetLastError(ERROR_INVALID_PARAMETER);
			return FALSE;
		}

		// Windows 7 has one processor group per affinity mask.
		if (Entry.Group != 0) {
			SetLastError(ERROR_NOT_SUPPORTED);
			return FALSE;
		}

		Result |= (KAFFINITY) 1 << Entry.LogicalProcessorIndex;
	}

	*Mask = Result;
	return TRUE;
}

STATIC ULONG KxIdsFromMask(
	OUT	PULONG	CpuSetIds	OPTIONAL,
	IN	ULONG	ArraySize,
	IN	KAFFINITY Mask)
{
	ULONG Count = 0, Bit;

	for (Bit = 0; Bit < 8 * sizeof(KAFFINITY); ++Bit) {
		if (!(Mask & ((KAFFINITY) 1 << Bit))) {
			continue;
		}

		if (CpuSetIds) {
			if (Count >= ArraySize) {
				break;
			}
			CpuSetIds[Count] = Bit;
		}

		++Count;
	}

	return Count;
}

STATIC ULONG KxMaskCount(IN KAFFINITY Mask)
{
	return KxIdsFromMask(NULL, 0, Mask);
}

KXBASEAPI BOOL WINAPI GetSystemCpuSetInformation(
	PVOID	Information,
	ULONG	BufferLength,
	PULONG	ReturnedLength,
	HANDLE	Process,
	ULONG	Flags)
{
	ULONG Count, Index, Required;
	KAFFINITY ProcessMask, SystemMask;
	DWORD SavedError = GetLastError();
	PKX_SYSTEM_CPU_SET_INFORMATION Output = (PKX_SYSTEM_CPU_SET_INFORMATION) Information;
	HANDLE TargetProcess = Process ? Process : GetCurrentProcess();

	if (ReturnedLength == NULL || Flags != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (!GetProcessAffinityMask(TargetProcess, &ProcessMask, &SystemMask)) {
		return FALSE;
	}

	Count = KxCpuSetCount();
	Required = Count * sizeof(KX_SYSTEM_CPU_SET_INFORMATION);

	if (Information == NULL || BufferLength < Required) {
		*ReturnedLength = Required;
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	for (Index = 0; Index < Count; ++Index) {
		ZeroMemory(&Output[Index], sizeof(Output[Index]));
		Output[Index].Size = sizeof(KX_SYSTEM_CPU_SET_INFORMATION);
		Output[Index].Type = 0; // CpuSetInformation
		Output[Index].CpuSet.Id = CpuSets[Index].Id;
		Output[Index].CpuSet.Group = CpuSets[Index].Group;
		Output[Index].CpuSet.LogicalProcessorIndex = CpuSets[Index].LogicalProcessorIndex;
		Output[Index].CpuSet.CoreIndex = CpuSets[Index].CoreIndex;
		Output[Index].CpuSet.LastLevelCacheIndex = CpuSets[Index].LastLevelCacheIndex;
		Output[Index].CpuSet.NumaNodeIndex = CpuSets[Index].NumaNodeIndex;
		Output[Index].CpuSet.EfficiencyClass = CpuSets[Index].EfficiencyClass;
		Output[Index].CpuSet.AllFlags = 0x2; // Allocated

		if (CpuSets[Index].Group == 0 &&
			(ProcessMask & ((KAFFINITY) 1 << CpuSets[Index].LogicalProcessorIndex))) {
			Output[Index].CpuSet.AllFlags |= 0x4; // AllocatedToTargetProcess
		}
	}

	*ReturnedLength = Required;
	SetLastError(SavedError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI GetProcessDefaultCpuSets(
	IN	HANDLE	ProcessHandle,
	OUT	PULONG	CpuSetIds,
	IN	ULONG	CpuSetIdArraySize,
	OUT	PULONG	ReturnCount)
{
	ULONG Count;
	KAFFINITY Mask = 0;
	DWORD SavedError = GetLastError();

	if (ReturnCount == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (CpuSetIds == NULL && CpuSetIdArraySize != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	AcquireSRWLockShared(&ProcessSetLock);
	if (ProcessDefaultValid) {
		Mask = ProcessDefaultMask;
	}
	ReleaseSRWLockShared(&ProcessSetLock);

	// Validate the handle even when no default set is recorded.
	{
		KAFFINITY ProcessMask, SystemMask;
		if (!GetProcessAffinityMask(ProcessHandle ? ProcessHandle : GetCurrentProcess(),
			&ProcessMask, &SystemMask)) {
			return FALSE;
		}
	}

	Count = KxMaskCount(Mask);

	if (Count == 0) {
		*ReturnCount = 0;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetIds == NULL) {
		*ReturnCount = Count;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetIdArraySize < Count) {
		*ReturnCount = Count;
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	*ReturnCount = KxIdsFromMask(CpuSetIds, CpuSetIdArraySize, Mask);
	SetLastError(SavedError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI SetProcessDefaultCpuSets(
	IN	HANDLE	ProcessHandle,
	IN	PULONG	CpuSetIds,
	IN	ULONG	NumberOfCpuSetIds)
{
	KAFFINITY Mask;
	KAFFINITY ProcessMask, SystemMask;
	DWORD SavedError = GetLastError();
	HANDLE TargetProcess = ProcessHandle ? ProcessHandle : GetCurrentProcess();

	if (!GetProcessAffinityMask(TargetProcess, &ProcessMask, &SystemMask)) {
		return FALSE;
	}

	if (!KxMaskFromCpuSetIds(CpuSetIds, NumberOfCpuSetIds, &Mask)) {
		return FALSE;
	}

	AcquireSRWLockExclusive(&ProcessSetLock);

	if (Mask == 0) {
		if (ProcessDefaultValid) {
			if (!SetProcessAffinityMask(TargetProcess, ProcessBaselineMask)) {
				ReleaseSRWLockExclusive(&ProcessSetLock);
				return FALSE;
			}
			ProcessDefaultValid = FALSE;
			ProcessDefaultMask = 0;
			ProcessBaselineMask = 0;
		}
	} else {
		if (!ProcessDefaultValid) {
			ProcessBaselineMask = ProcessMask;
		}

		if (!SetProcessAffinityMask(TargetProcess, Mask)) {
			ReleaseSRWLockExclusive(&ProcessSetLock);
			return FALSE;
		}

		ProcessDefaultMask = Mask;
		ProcessDefaultValid = TRUE;
	}

	ReleaseSRWLockExclusive(&ProcessSetLock);
	SetLastError(SavedError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI GetProcessDefaultCpuSetMasks(
	IN	HANDLE			ProcessHandle,
	OUT	PGROUP_AFFINITY	CpuSetMasks,
	IN	ULONG			CpuSetMaskArraySize,
	OUT	PULONG			ReturnCount)
{
	KAFFINITY Mask = 0;
	DWORD SavedError = GetLastError();
	KAFFINITY ProcessMask, SystemMask;

	if (ReturnCount == NULL || (CpuSetMasks == NULL && CpuSetMaskArraySize != 0)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (!GetProcessAffinityMask(ProcessHandle ? ProcessHandle : GetCurrentProcess(),
		&ProcessMask, &SystemMask)) {
		return FALSE;
	}

	AcquireSRWLockShared(&ProcessSetLock);
	if (ProcessDefaultValid) {
		Mask = ProcessDefaultMask;
	}
	ReleaseSRWLockShared(&ProcessSetLock);

	if (Mask == 0) {
		*ReturnCount = 0;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetMasks == NULL) {
		*ReturnCount = 1;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetMaskArraySize < 1) {
		*ReturnCount = 1;
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	ZeroMemory(CpuSetMasks, sizeof(*CpuSetMasks));
	CpuSetMasks[0].Mask = Mask;
	CpuSetMasks[0].Group = 0;
	*ReturnCount = 1;
	SetLastError(SavedError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI SetProcessDefaultCpuSetMasks(
	IN	HANDLE			ProcessHandle,
	IN	PGROUP_AFFINITY	CpuSetMasks,
	IN	ULONG			NumberOfCpuSetMasks)
{
	KAFFINITY Mask = 0;
	ULONG Index;
	KAFFINITY ProcessMask, SystemMask;
	DWORD SavedError = GetLastError();
	HANDLE TargetProcess = ProcessHandle ? ProcessHandle : GetCurrentProcess();

	if (NumberOfCpuSetMasks != 0 && CpuSetMasks == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (!GetProcessAffinityMask(TargetProcess, &ProcessMask, &SystemMask)) {
		return FALSE;
	}

	for (Index = 0; Index < NumberOfCpuSetMasks; ++Index) {
		if (CpuSetMasks[Index].Group != 0 || CpuSetMasks[Index].Reserved[0] ||
			CpuSetMasks[Index].Reserved[1] || CpuSetMasks[Index].Reserved[2]) {
			SetLastError(ERROR_NOT_SUPPORTED);
			return FALSE;
		}
		Mask |= CpuSetMasks[Index].Mask;
	}

	AcquireSRWLockExclusive(&ProcessSetLock);

	if (Mask == 0) {
		if (ProcessDefaultValid) {
			if (!SetProcessAffinityMask(TargetProcess, ProcessBaselineMask)) {
				ReleaseSRWLockExclusive(&ProcessSetLock);
				return FALSE;
			}
			ProcessDefaultValid = FALSE;
			ProcessDefaultMask = 0;
			ProcessBaselineMask = 0;
		}
	} else {
		if (!ProcessDefaultValid) {
			ProcessBaselineMask = ProcessMask;
		}

		if (!SetProcessAffinityMask(TargetProcess, Mask)) {
			ReleaseSRWLockExclusive(&ProcessSetLock);
			return FALSE;
		}

		ProcessDefaultMask = Mask;
		ProcessDefaultValid = TRUE;
	}

	ReleaseSRWLockExclusive(&ProcessSetLock);
	SetLastError(SavedError);
	return TRUE;
}

STATIC BOOLEAN KxThreadIdentity(
	IN	HANDLE		ThreadHandle,
	OUT	PDWORD		ThreadId,
	OUT	PULONGLONG	CreationTime)
{
	FILETIME Creation, Exit, Kernel, User;

	if (ThreadHandle == NULL || ThreadHandle == GetCurrentThread()) {
		*ThreadId = GetCurrentThreadId();
	} else {
		*ThreadId = GetThreadId(ThreadHandle);
	}
	if (*ThreadId == 0) {
		return FALSE;
	}

	*CreationTime = 0;
	if (GetThreadTimes(ThreadHandle, &Creation, &Exit, &Kernel, &User)) {
		*CreationTime = ((ULONGLONG) Creation.dwHighDateTime << 32) | Creation.dwLowDateTime;
	}

	return TRUE;
}

STATIC PKX_THREAD_CPU_SET KxFindThreadEntry(
	IN	DWORD		ThreadId,
	IN	ULONGLONG	CreationTime)
{
	ULONG Index;

	for (Index = 0; Index < KX_MAX_THREAD_SETS; ++Index) {
		if (ThreadSets[Index].Valid && ThreadSets[Index].ThreadId == ThreadId &&
			ThreadSets[Index].CreationTime == CreationTime) {
			return &ThreadSets[Index];
		}
	}

	return NULL;
}

STATIC PKX_THREAD_CPU_SET KxAllocateThreadEntry(
	IN	DWORD		ThreadId,
	IN	ULONGLONG	CreationTime)
{
	ULONG Index;

	for (Index = 0; Index < KX_MAX_THREAD_SETS; ++Index) {
		if (!ThreadSets[Index].Valid) {
			break;
		}
	}

	if (Index == KX_MAX_THREAD_SETS) {
		Index = 0;
	}

	ThreadSets[Index].Valid = TRUE;
	ThreadSets[Index].ThreadId = ThreadId;
	ThreadSets[Index].CreationTime = CreationTime;
	ThreadSets[Index].Selected = 0;
	ThreadSets[Index].Baseline = 0;
	return &ThreadSets[Index];
}

STATIC BOOLEAN KxQueryThreadAffinity(IN HANDLE ThreadHandle, OUT PKAFFINITY Mask)
{
	GROUP_AFFINITY Affinity;

	if (!GetThreadGroupAffinity(ThreadHandle, &Affinity)) {
		return FALSE;
	}

	*Mask = Affinity.Mask;
	return TRUE;
}

KXBASEAPI BOOL WINAPI GetThreadSelectedCpuSets(
	IN	HANDLE	ThreadHandle,
	OUT	PULONG	CpuSetIds,
	IN	ULONG	CpuSetIdArraySize,
	OUT	PULONG	ReturnCount)
{
	DWORD ThreadId;
	ULONGLONG CreationTime;
	PKX_THREAD_CPU_SET Entry;
	KAFFINITY Mask = 0;
	ULONG Count;
	DWORD SavedError = GetLastError();

	if (ReturnCount == NULL || (CpuSetIds == NULL && CpuSetIdArraySize != 0)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (!KxThreadIdentity(ThreadHandle ? ThreadHandle : GetCurrentThread(),
		&ThreadId, &CreationTime)) {
		return FALSE;
	}

	AcquireSRWLockShared(&ThreadSetLock);
	Entry = KxFindThreadEntry(ThreadId, CreationTime);
	if (Entry) {
		Mask = Entry->Selected;
	}
	ReleaseSRWLockShared(&ThreadSetLock);

	Count = KxMaskCount(Mask);

	if (Count == 0) {
		*ReturnCount = 0;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetIds == NULL) {
		*ReturnCount = Count;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetIdArraySize < Count) {
		*ReturnCount = Count;
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	*ReturnCount = KxIdsFromMask(CpuSetIds, CpuSetIdArraySize, Mask);
	SetLastError(SavedError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI SetThreadSelectedCpuSets(
	IN	HANDLE	ThreadHandle,
	IN	PULONG	CpuSetIds,
	IN	ULONG	NumberOfCpuSetIds)
{
	KAFFINITY Mask, Baseline;
	DWORD ThreadId, SavedError = GetLastError();
	ULONGLONG CreationTime;
	PKX_THREAD_CPU_SET Entry;
	HANDLE TargetThread = ThreadHandle ? ThreadHandle : GetCurrentThread();

	if (!KxMaskFromCpuSetIds(CpuSetIds, NumberOfCpuSetIds, &Mask)) {
		return FALSE;
	}

	if (!KxThreadIdentity(TargetThread, &ThreadId, &CreationTime)) {
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}

	if (!KxQueryThreadAffinity(TargetThread, &Baseline)) {
		return FALSE;
	}

	AcquireSRWLockExclusive(&ThreadSetLock);

	if (Mask == 0) {
		Entry = KxFindThreadEntry(ThreadId, CreationTime);
		if (Entry && Entry->Baseline != 0) {
			if (!SetThreadAffinityMask(TargetThread, Entry->Baseline)) {
				ReleaseSRWLockExclusive(&ThreadSetLock);
				return FALSE;
			}
		}
		if (Entry) {
			Entry->Valid = FALSE;
		}
	} else {
		if (!SetThreadAffinityMask(TargetThread, Mask)) {
			ReleaseSRWLockExclusive(&ThreadSetLock);
			return FALSE;
		}

		Entry = KxFindThreadEntry(ThreadId, CreationTime);
		if (!Entry) {
			Entry = KxAllocateThreadEntry(ThreadId, CreationTime);
		}
		Entry->Baseline = Baseline;
		Entry->Selected = Mask;
	}

	ReleaseSRWLockExclusive(&ThreadSetLock);
	SetLastError(SavedError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI GetThreadSelectedCpuSetMasks(
	IN	HANDLE			ThreadHandle,
	OUT	PGROUP_AFFINITY	CpuSetMasks,
	IN	ULONG			CpuSetMaskArraySize,
	OUT	PULONG			ReturnCount)
{
	DWORD ThreadId;
	ULONGLONG CreationTime;
	PKX_THREAD_CPU_SET Entry;
	KAFFINITY Mask = 0;
	DWORD SavedError = GetLastError();

	if (ReturnCount == NULL || (CpuSetMasks == NULL && CpuSetMaskArraySize != 0)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (!KxThreadIdentity(ThreadHandle ? ThreadHandle : GetCurrentThread(),
		&ThreadId, &CreationTime)) {
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}

	AcquireSRWLockShared(&ThreadSetLock);
	Entry = KxFindThreadEntry(ThreadId, CreationTime);
	if (Entry) {
		Mask = Entry->Selected;
	}
	ReleaseSRWLockShared(&ThreadSetLock);

	if (Mask == 0) {
		*ReturnCount = 0;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetMasks == NULL) {
		*ReturnCount = 1;
		SetLastError(SavedError);
		return TRUE;
	}

	if (CpuSetMaskArraySize < 1) {
		*ReturnCount = 1;
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	ZeroMemory(CpuSetMasks, sizeof(*CpuSetMasks));
	CpuSetMasks[0].Mask = Mask;
	CpuSetMasks[0].Group = 0;
	*ReturnCount = 1;
	SetLastError(SavedError);
	return TRUE;
}

KXBASEAPI BOOL WINAPI SetThreadSelectedCpuSetMasks(
	IN	HANDLE			ThreadHandle,
	IN	PGROUP_AFFINITY	CpuSetMasks,
	IN	ULONG			NumberOfCpuSetMasks)
{
	KAFFINITY Mask = 0, Baseline;
	ULONG Index;
	DWORD ThreadId, SavedError = GetLastError();
	ULONGLONG CreationTime;
	PKX_THREAD_CPU_SET Entry;
	HANDLE TargetThread = ThreadHandle ? ThreadHandle : GetCurrentThread();

	if (NumberOfCpuSetMasks != 0 && CpuSetMasks == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	for (Index = 0; Index < NumberOfCpuSetMasks; ++Index) {
		if (CpuSetMasks[Index].Group != 0 || CpuSetMasks[Index].Reserved[0] ||
			CpuSetMasks[Index].Reserved[1] || CpuSetMasks[Index].Reserved[2]) {
			SetLastError(ERROR_NOT_SUPPORTED);
			return FALSE;
		}
		Mask |= CpuSetMasks[Index].Mask;
	}

	if (!KxThreadIdentity(TargetThread, &ThreadId, &CreationTime)) {
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}

	if (!KxQueryThreadAffinity(TargetThread, &Baseline)) {
		return FALSE;
	}

	AcquireSRWLockExclusive(&ThreadSetLock);

	if (Mask == 0) {
		Entry = KxFindThreadEntry(ThreadId, CreationTime);
		if (Entry && Entry->Baseline != 0) {
			if (!SetThreadAffinityMask(TargetThread, Entry->Baseline)) {
				ReleaseSRWLockExclusive(&ThreadSetLock);
				return FALSE;
			}
		}
		if (Entry) {
			Entry->Valid = FALSE;
		}
	} else {
		if (!SetThreadAffinityMask(TargetThread, Mask)) {
			ReleaseSRWLockExclusive(&ThreadSetLock);
			return FALSE;
		}

		Entry = KxFindThreadEntry(ThreadId, CreationTime);
		if (!Entry) {
			Entry = KxAllocateThreadEntry(ThreadId, CreationTime);
		}
		Entry->Baseline = Baseline;
		Entry->Selected = Mask;
	}

	ReleaseSRWLockExclusive(&ThreadSetLock);
	SetLastError(SavedError);
	return TRUE;
}
//</CPU_SET_TEST_BODY>
