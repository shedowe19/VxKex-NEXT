///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     dllpath.c
//
// Abstract:
//
//     Contains routines for adding the Kex32/64 directory to the loader
//     search path, so that the process can load our extended DLLs. The
//     loader search path is a UNICODE_STRING structure, contained within
//     Peb->ProcessParameters->DllPath. This string contains semicolon-
//     separated Win32 paths, which is the same format as used in the %Path%
//     environment variable.
//
//     This is a relatively complex problem with a number of constraints.
//
//       1. We need to add KexDir\Kex32 or KexDir\Kex64 to the loader search
//          path, based on whether this is a 32 bit or 64 bit process.
//
//       2. The old length of the search path must be maintained, as the loader
//          caches the length value. Therefore, the search path cannot be
//          extended or contracted.
//
//       3. We must not disturb the presence or the order of existing search
//          path entries at all.
//
//     Fortunately, a number of factors make this possible in most cases:
//
//       1. The loader does not cache the actual data inside DllPath.Buffer,
//          so we can modify this buffer and influence the loader.
//
//   2 & 3. We can contract the length of the search path by removing
//          duplicate path entries. For example, %WinDir%\system32,
//          %WinDir%\system, and %WinDir% are all almost always duplicated
//          (since the system places them at the beginning, and then pastes
//          the %Path% environment variable after them, which contains
//          identical entries).
//
//          Sometimes, it is possible to collapse the length by removing
//          duplicate semicolons; however, this is much less likely to produce
//          a useful result and the current code implementation does not use
//          this method.
//
//          We can also pad the length of the search path out to the original
//          length by simply filling unused space with semicolons.
//
//   Keep in mind that the loader search path described above is only applicable
//   to process initialization AND when LdrLoadDll is called with NULL as the
//   first parameter (the DllPath parameter).
//
//   When DLLs are loaded using Kernel32/KernelBase functions such as
//   LoadLibrary, then the BASE dlls supply their own DllPath parameter to
//   LdrLoadDll which is passed down throughout the entire DLL load sequence
//   including the resolution of static imports. Therefore, the default loader
//   DllPath which is accessible through the PEB is not applicable after the
//   end of process initialization (except for the unlikely event of someone
//   calling LdrLoadDll with NULL as the first parameter).
//
// Author:
//
//     vxiiduu (30-Oct-2022)
//
// Revision History:
//
//     vxiiduu              30-Oct-2022  Initial creation.
//     vxiiduu              31-Oct-2022  Fix bugs and finalize implementation.
//     vxiiduu              18-Mar-2024  Fix a bug where forward slashes in the
//                                       DllPath could impede the ability of
//                                       KexpShrinkDllPathLength to do its job.
//     vxiiduu              05-Apr-2024  Correct more bugs.
//     vxiiduu              16-May-2025  Major rewrite of KexpShrinkDllPathLength
//                                       to enable it to detect duplicate path
//                                       elements at the end of the DLL path, stop
//                                       it from adding a spurious null character
//                                       to the end of the DLL path, and add
//                                       comments to clarify what the code is doing.
//
///////////////////////////////////////////////////////////////////////////////

#include "buildcfg.h"
#include "kexdllp.h"

STATIC INLINE NTSTATUS KexpShrinkDllPathLength(
	IN	PUNICODE_STRING	DllPath,
	IN	USHORT			TargetLength);

STATIC INLINE NTSTATUS KexpPadDllPathToOriginalLength(
	IN	PUNICODE_STRING	DllPath,
	IN	USHORT			OriginalLength);

STATIC INLINE VOID KexpNormalizeDllPathBackslashes(
	IN OUT	PUNICODE_STRING	DllPath);

STATIC INLINE VOID KexpReplaceDllPathEmbeddedNulls(
	IN OUT	PUNICODE_STRING	DllPath);

BOOL KexShouldUseWorkaroundsForNewerWindows(
	VOID)
{
	STATIC INT ShouldUseWorkaroundsForNewerWindows = -1;
	if (ShouldUseWorkaroundsForNewerWindows == -1) {
		NTSTATUS Status;
		ANSI_STRING LdrAddDllDirectoryName;
		NTSTATUS(NTAPI *pLdrAddDllDirectory) (PCUNICODE_STRING, PPVOID);
		RtlInitAnsiString(&LdrAddDllDirectoryName, "LdrAddDllDirectory");
		Status = LdrGetProcedureAddress(KexLdrGetSystemDllBase(), &LdrAddDllDirectoryName, FALSE, (PPVOID)&pLdrAddDllDirectory);
		ShouldUseWorkaroundsForNewerWindows = NT_SUCCESS(Status);
	}
	return ShouldUseWorkaroundsForNewerWindows;
}

NTSTATUS KexpAddKex3264ToDllPath(
	VOID)
{
	NTSTATUS Status;
	PUNICODE_STRING DllPath;
	UNICODE_STRING NewDllPath;
	UNICODE_STRING ShortDllPath;
	USHORT DllPathOriginalLength;
	USHORT BufferLength;
	ANSI_STRING ProcedureName;
	NTSTATUS (NTAPI *pLdrAddDllDirectory) (PCUNICODE_STRING, PPVOID);

	ASSERT (VALID_UNICODE_STRING(&KexData->Kex3264DirPath));
	ASSERT (KexData->Kex3264DirPath.Length != 0);

	DllPath = &NtCurrentPeb()->ProcessParameters->DllPath;
	DllPathOriginalLength = DllPath->Length;

	RtlInitAnsiString(&ProcedureName, "LdrAddDllDirectory");
	Status = LdrGetProcedureAddress(KexData->SystemDllBase, &ProcedureName,
		FALSE, (PPVOID) &pLdrAddDllDirectory);

	if (NT_SUCCESS(Status) && DllPathOriginalLength == 0) {
		PVOID Cookie;
		NTSTATUS (NTAPI *pLdrRemoveDllDirectory) (PVOID);
		UNICODE_STRING PathName;
		UNICODE_STRING OriginalPath;
		UNICODE_STRING Path;
		SIZE_T OriginalPathLength;
		SIZE_T PathBufferLength;

		OriginalPath.Length = 0;
		OriginalPath.MaximumLength = 0;
		OriginalPath.Buffer = NULL;
		Path.Buffer = NULL;
		OriginalPathLength = 0;
		RtlInitUnicodeString(&PathName, L"Path");

		Status = RtlQueryEnvironmentVariable(NULL, PathName.Buffer,
			PathName.Length / sizeof(WCHAR), NULL, 0, &OriginalPathLength);
		if (Status != STATUS_VARIABLE_NOT_FOUND &&
			Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status)) {
			return Status;
		}

		if (Status != STATUS_VARIABLE_NOT_FOUND && OriginalPathLength != 0) {
			if (OriginalPathLength > ((USHORT) -1) / sizeof(WCHAR)) {
				return STATUS_BUFFER_TOO_SMALL;
			}
			OriginalPath.MaximumLength = (USHORT) (OriginalPathLength * sizeof(WCHAR));
			OriginalPath.Buffer = SafeAlloc(WCHAR, OriginalPathLength);
			if (!OriginalPath.Buffer) {
				return STATUS_NO_MEMORY;
			}
			Status = RtlQueryEnvironmentVariable_U(NULL, &PathName, &OriginalPath);
			if (!NT_SUCCESS(Status)) {
				goto EnvironmentExit;
			}
		}

		// Reserve a terminator, and a separator only when PATH has contents.
		PathBufferLength = KexData->Kex3264DirPath.Length + sizeof(WCHAR);
		if (OriginalPath.Length != 0) {
			PathBufferLength += OriginalPath.Length + sizeof(WCHAR);
		}
		if (PathBufferLength > (USHORT) -1) {
			Status = STATUS_BUFFER_TOO_SMALL;
			goto EnvironmentExit;
		}
		Path.Length = 0;
		Path.MaximumLength = (USHORT) PathBufferLength;
		Path.Buffer = SafeAlloc(WCHAR, PathBufferLength / sizeof(WCHAR));
		if (!Path.Buffer) {
			Status = STATUS_NO_MEMORY;
			goto EnvironmentExit;
		}

		Status = RtlAppendUnicodeStringToString(&Path, &KexData->Kex3264DirPath);
		if (!NT_SUCCESS(Status)) goto EnvironmentExit;
		if (OriginalPath.Length != 0) {
			Status = RtlAppendUnicodeToString(&Path, L";");
			if (!NT_SUCCESS(Status)) goto EnvironmentExit;
			Status = RtlAppendUnicodeStringToString(&Path, &OriginalPath);
			if (!NT_SUCCESS(Status)) goto EnvironmentExit;
		}
		Status = KexRtlNullTerminateUnicodeString(&Path);
		if (!NT_SUCCESS(Status)) goto EnvironmentExit;

		// Resolve rollback before registering a directory. An environment update
		// failure must not leave half of the search-path change installed.
		RtlInitAnsiString(&ProcedureName, "LdrRemoveDllDirectory");
		Status = LdrGetProcedureAddress(KexData->SystemDllBase, &ProcedureName,
			FALSE, (PPVOID) &pLdrRemoveDllDirectory);
		if (!NT_SUCCESS(Status)) goto EnvironmentExit;
		Status = pLdrAddDllDirectory(&KexData->Kex3264DirPath, &Cookie);
		if (!NT_SUCCESS(Status)) goto EnvironmentExit;
		Status = RtlSetEnvironmentVariable(NULL, &PathName, &Path);
		if (!NT_SUCCESS(Status)) {
			NTSTATUS RemoveStatus;
			RemoveStatus = pLdrRemoveDllDirectory(Cookie);
			ASSERT (NT_SUCCESS(RemoveStatus));
		}

EnvironmentExit:
		SafeFree(Path.Buffer);
		SafeFree(OriginalPath.Buffer);
		return Status;
	}

	if (!DllPath->Buffer || DllPath->Length > DllPath->MaximumLength ||
		DllPath->Length % sizeof(WCHAR) != 0) {
		return STATUS_INVALID_PARAMETER;
	}
	if (DllPathOriginalLength <= KexData->Kex3264DirPath.Length) {
		return STATUS_PATH_TOO_SHORT;
	}
	if ((SIZE_T) DllPathOriginalLength + sizeof(WCHAR) > (USHORT) -1) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	// Work on copies. Normalization, duplicate removal and failed appends must
	// never modify the loader's buffer or its cached length on an error path.
	BufferLength = DllPathOriginalLength + sizeof(WCHAR);
	ShortDllPath.Length = 0;
	ShortDllPath.MaximumLength = BufferLength;
	ShortDllPath.Buffer = SafeAlloc(WCHAR, BufferLength / sizeof(WCHAR));
	if (!ShortDllPath.Buffer) {
		return STATUS_NO_MEMORY;
	}
	NewDllPath.Length = 0;
	NewDllPath.MaximumLength = BufferLength;
	NewDllPath.Buffer = SafeAlloc(WCHAR, BufferLength / sizeof(WCHAR));
	if (!NewDllPath.Buffer) {
		SafeFree(ShortDllPath.Buffer);
		return STATUS_NO_MEMORY;
	}

	RtlCopyUnicodeString(&ShortDllPath, DllPath);
	KexpReplaceDllPathEmbeddedNulls(&ShortDllPath);
	KexpNormalizeDllPathBackslashes(&ShortDllPath);
	Status = KexpShrinkDllPathLength(&ShortDllPath,
		DllPathOriginalLength - (KexData->Kex3264DirPath.Length + sizeof(WCHAR)));
	if (!NT_SUCCESS(Status)) goto Exit;

	Status = RtlAppendUnicodeStringToString(&NewDllPath, &KexData->Kex3264DirPath);
	if (!NT_SUCCESS(Status)) goto Exit;
	Status = RtlAppendUnicodeToString(&NewDllPath, L";");
	if (!NT_SUCCESS(Status)) goto Exit;
	Status = RtlAppendUnicodeStringToString(&NewDllPath, &ShortDllPath);
	if (!NT_SUCCESS(Status)) goto Exit;
	Status = KexpPadDllPathToOriginalLength(&NewDllPath, DllPathOriginalLength);
	if (!NT_SUCCESS(Status)) goto Exit;

	RtlCopyMemory(DllPath->Buffer, NewDllPath.Buffer, DllPathOriginalLength);
	if (DllPath->MaximumLength - DllPathOriginalLength >= sizeof(WCHAR)) {
		DllPath->Buffer[DllPathOriginalLength / sizeof(WCHAR)] = L'\0';
	}
	ASSERT (DllPath->Length == DllPathOriginalLength);

Exit:
	SafeFree(NewDllPath.Buffer);
	SafeFree(ShortDllPath.Buffer);
	return Status;
}

//
// Try to shrink the DllPath length to equal or less than "Length" by
// removing duplicate entries.
//
STATIC INLINE NTSTATUS KexpShrinkDllPathLength(
	IN	PUNICODE_STRING	DllPath,
	IN	USHORT			TargetLength)
{
	ULONG CurrentStart;
	ULONG PathCch;

	PathCch = KexRtlUnicodeStringCch(DllPath);
	CurrentStart = 0;
	while (DllPath->Length > TargetLength && CurrentStart < PathCch) {
		ULONG CurrentEnd;
		ULONG DuplicateStart;
		UNICODE_STRING CurrentEntry;

		while (CurrentStart < PathCch && DllPath->Buffer[CurrentStart] == L';') {
			++CurrentStart;
		}
		CurrentEnd = CurrentStart;
		while (CurrentEnd < PathCch && DllPath->Buffer[CurrentEnd] != L';') {
			++CurrentEnd;
		}
		if (CurrentEnd == CurrentStart) break;
		CurrentEntry.Buffer = DllPath->Buffer + CurrentStart;
		CurrentEntry.Length = (USHORT) ((CurrentEnd - CurrentStart) * sizeof(WCHAR));
		CurrentEntry.MaximumLength = CurrentEntry.Length;
		DuplicateStart = CurrentEnd;

		while (DllPath->Length > TargetLength && DuplicateStart < PathCch) {
			ULONG DuplicateEnd;
			UNICODE_STRING DuplicateEntry;

			while (DuplicateStart < PathCch && DllPath->Buffer[DuplicateStart] == L';') {
				++DuplicateStart;
			}
			DuplicateEnd = DuplicateStart;
			while (DuplicateEnd < PathCch && DllPath->Buffer[DuplicateEnd] != L';') {
				++DuplicateEnd;
			}
			DuplicateEntry.Buffer = DllPath->Buffer + DuplicateStart;
			DuplicateEntry.Length = (USHORT) ((DuplicateEnd - DuplicateStart) * sizeof(WCHAR));
			DuplicateEntry.MaximumLength = DuplicateEntry.Length;
			if (RtlEqualUnicodeString(&CurrentEntry, &DuplicateEntry, TRUE)) {
				ULONG RemovedCch;

				// Remove the duplicate and its preceding separator. Move, rather
				// than copy, because the source and destination overlap.
				RemovedCch = DuplicateEnd - DuplicateStart + 1;
				RtlMoveMemory(DllPath->Buffer + DuplicateStart - 1,
					DllPath->Buffer + DuplicateEnd,
					(PathCch - DuplicateEnd) * sizeof(WCHAR));
				PathCch -= RemovedCch;
				DllPath->Length = (USHORT) (PathCch * sizeof(WCHAR));
				DuplicateStart -= 1;
			} else {
				DuplicateStart = DuplicateEnd;
			}
		}
		CurrentStart = CurrentEnd;
	}

	if (DllPath->Length > TargetLength) {
		return STATUS_PATH_TOO_SHORT;
	}
	return KexRtlNullTerminateUnicodeString(DllPath);
}

STATIC INLINE NTSTATUS KexpPadDllPathToOriginalLength(
	IN	PUNICODE_STRING	DllPath,
	IN	USHORT			OriginalLength)
{
	PWCHAR Pointer;

	if (DllPath->Length > OriginalLength ||
		OriginalLength > DllPath->MaximumLength ||
		DllPath->MaximumLength - OriginalLength < sizeof(WCHAR)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	//
	// Add semicolons to the end until DllPath reaches the correct length.
	//

	Pointer = KexRtlEndOfUnicodeString(DllPath);
	DllPath->Length = OriginalLength;

	while (Pointer < KexRtlEndOfUnicodeString(DllPath)) {
		*Pointer++ = ';';
	}

	return KexRtlNullTerminateUnicodeString(DllPath);
}

//
// Convert all slashes in the specified DllPath to backslashes.
//
STATIC INLINE VOID KexpNormalizeDllPathBackslashes(
	IN OUT	PUNICODE_STRING	DllPath)
{
	ULONG Index;
	ULONG DllPathCch;

	ASSUME (VALID_UNICODE_STRING(DllPath));

	DllPathCch = KexRtlUnicodeStringCch(DllPath);
	ASSUME (DllPathCch > 0);

	for (Index = 0; Index < DllPathCch; ++Index) {
		if (DllPath->Buffer[Index] == '/') {
			DllPath->Buffer[Index] = '\\';
		}
	}
}

//
// Replace embedded null \0 characters with semicolons. They can cause
// problems with logging.
//
STATIC INLINE VOID KexpReplaceDllPathEmbeddedNulls(
	IN OUT	PUNICODE_STRING	DllPath)
{
	ULONG Index;

	for (Index = 0; Index < KexRtlUnicodeStringCch(DllPath); ++Index) {
		if (DllPath->Buffer[Index] == '\0') {
			//KexDebugCheckpoint();
			DllPath->Buffer[Index] = ';';
		}
	}

	ASSERT (!KexRtlUnicodeStringContainsEmbeddedNull(DllPath));
}