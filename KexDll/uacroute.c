// Route only selected, already-bound application imports. Native exports and
// executable pages remain untouched; another component's IAT hook always wins.
#ifdef KEX_UAC_TEST
#include "uac_test_shim.h"
#else
#include "buildcfg.h"
#include "kexdllp.h"
#endif

typedef HINSTANCE (WINAPI *KEX_UAC_SHELL_A)(HWND, PCSTR, PCSTR, PCSTR, PCSTR, INT);
typedef HINSTANCE (WINAPI *KEX_UAC_SHELL_W)(HWND, PCWSTR, PCWSTR, PCWSTR, PCWSTR, INT);
typedef BOOL (WINAPI *KEX_UAC_SHELL_EX)(PVOID);
typedef HMODULE (WINAPI *KEX_UAC_LOAD_A)(PCSTR);
typedef HMODULE (WINAPI *KEX_UAC_LOAD_W)(PCWSTR);
typedef HMODULE (WINAPI *KEX_UAC_LOAD_EX_A)(PCSTR, HANDLE, ULONG);
typedef HMODULE (WINAPI *KEX_UAC_LOAD_EX_W)(PCWSTR, HANDLE, ULONG);

static HINSTANCE WINAPI KexpUacShellA(HWND, PCSTR, PCSTR, PCSTR, PCSTR, INT);
static HINSTANCE WINAPI KexpUacShellW(HWND, PCWSTR, PCWSTR, PCWSTR, PCWSTR, INT);
static BOOL WINAPI KexpUacShellExA(PVOID);
static BOOL WINAPI KexpUacShellExW(PVOID);
static HMODULE WINAPI KexpUacLoadA(PCSTR);
static HMODULE WINAPI KexpUacLoadW(PCWSTR);
static HMODULE WINAPI KexpUacLoadExA(PCSTR, HANDLE, ULONG);
static HMODULE WINAPI KexpUacLoadExW(PCWSTR, HANDLE, ULONG);

typedef struct {
    PCSTR Name;
    PVOID Native;
    PVOID Replacement;
} KEX_UAC_ROUTE;

static KEX_UAC_ROUTE KexpUacRoutes[] = {
    {"ShellExecuteA", NULL, KexpUacShellA},
    {"ShellExecuteW", NULL, KexpUacShellW},
    {"ShellExecuteExA", NULL, KexpUacShellExA},
    {"ShellExecuteExW", NULL, KexpUacShellExW},
    {"LoadLibraryA", NULL, KexpUacLoadA},
    {"LoadLibraryW", NULL, KexpUacLoadW},
    {"LoadLibraryExA", NULL, KexpUacLoadExA},
    {"LoadLibraryExW", NULL, KexpUacLoadExW}
};
static RTL_SRWLOCK KexpUacPatchLock;
static PVOID KexpUacProvider;
static PVOID KexpUacProviderFunctions[4];
static BOOLEAN KexpUacProviderLoading;

// Loader lock is always acquired before PatchLock. These lookups do not load
// shell32: a process which never used the shell does not acquire that dependency.
static VOID KexpUacResolveNative(VOID)
{
    UNICODE_STRING ModuleName;
    ANSI_STRING ProcedureName;
    PVOID Module, Procedure;
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(KexpUacRoutes); ++Index) {
        RtlInitUnicodeString(&ModuleName,
            Index < 4 ? L"shell32.dll" : L"kernel32.dll");
        Module = NULL;
        if (!NT_SUCCESS(LdrGetDllHandle(NULL, NULL, &ModuleName, &Module))) continue;
        RtlInitAnsiString(&ProcedureName, KexpUacRoutes[Index].Name);
        Procedure = NULL;
        if (NT_SUCCESS(LdrGetProcedureAddress(Module, &ProcedureName, 0, &Procedure)) &&
            Procedure) {
            InterlockedExchangePointer(&KexpUacRoutes[Index].Native, Procedure);
        }
    }
}

static BOOLEAN KexpUacNameEqual(PCSTR Left, PCSTR Right)
{
    ULONG Index;
    for (Index = 0; Index < 64; ++Index) {
        if (Left[Index] != Right[Index]) return FALSE;
        if (!Left[Index]) return TRUE;
    }
    return FALSE;
}

static BOOLEAN KexpUacPathWithin(PCUNICODE_STRING Path, PCUNICODE_STRING Root)
{
    USHORT Offset;
    if (!Root->Length || !Root->Buffer || !Path->Buffer ||
        !RtlPrefixUnicodeString(Root, Path, TRUE)) return FALSE;
    Offset = Root->Length / sizeof(WCHAR);
    return Path->Length == Root->Length || Root->Buffer[Offset - 1] == L'\\' ||
        Path->Buffer[Offset] == L'\\';
}

static BOOLEAN KexpUacRange(ULONG Rva, SIZE_T Length, ULONG Size)
{
    return Rva < Size && Length <= (SIZE_T) Size - Rva;
}

static BOOLEAN KexpUacDataSlot(PIMAGE_SECTION_HEADER Sections, USHORT Count,
    ULONG Rva, ULONG Size)
{
    USHORT Index;
    ULONG Span;
    BOOLEAN Found;
    Found = FALSE;
    for (Index = 0; Index < Count; ++Index) {
        Span = max(Sections[Index].Misc.VirtualSize, Sections[Index].SizeOfRawData);
        if (!Span || !KexpUacRange(Sections[Index].VirtualAddress, Span, Size)) continue;
        if (Rva >= Sections[Index].VirtualAddress &&
            Rva - Sections[Index].VirtualAddress < Span) {
            if ((Sections[Index].Characteristics & IMAGE_SCN_MEM_EXECUTE) ||
                sizeof(PVOID) > Span - (Rva - Sections[Index].VirtualAddress)) return FALSE;
            Found = TRUE;
        }
    }
    return Found;
}

static NTSTATUS KexpUacPatchSlot(PVOID *Slot, PVOID Native, PVOID Replacement)
{
    NTSTATUS Status, RestoreStatus;
    MEMORY_BASIC_INFORMATION Information;
    SIZE_T Returned, RegionSize;
    PVOID RegionBase;
    ULONG OldProtect, IgnoredProtect;
    BOOLEAN Changed;

    if (((ULONG_PTR) Slot & (sizeof(PVOID) - 1)) || *Slot != Native)
        return STATUS_SUCCESS;
    Status = NtQueryVirtualMemory(NtCurrentProcess(), Slot, MemoryBasicInformation,
        &Information, sizeof(Information), &Returned);
    if (!NT_SUCCESS(Status)) return Status;
    if (Information.State != MEM_COMMIT || Information.Type != MEM_IMAGE ||
        (Information.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_GUARD | PAGE_NOACCESS)) ||
        (ULONG_PTR) Slot < (ULONG_PTR) Information.BaseAddress ||
        (ULONG_PTR) Slot - (ULONG_PTR) Information.BaseAddress > Information.RegionSize ||
        sizeof(PVOID) > Information.RegionSize -
        ((ULONG_PTR) Slot - (ULONG_PTR) Information.BaseAddress)) return STATUS_SUCCESS;

    Changed = FALSE;
    __try {
        RegionBase = Slot;
        RegionSize = sizeof(PVOID);
        Status = NtProtectVirtualMemory(NtCurrentProcess(), &RegionBase,
            &RegionSize, PAGE_READWRITE, &OldProtect);
        if (!NT_SUCCESS(Status)) __leave;
        Changed = TRUE;
        if (!(OldProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            InterlockedCompareExchangePointer(Slot, Replacement, Native);
        }
    } __finally {
        if (Changed) {
            RestoreStatus = NtProtectVirtualMemory(NtCurrentProcess(), &RegionBase,
                &RegionSize, OldProtect, &IgnoredProtect);
            if (!NT_SUCCESS(RestoreStatus)) Status = RestoreStatus;
        }
    }
    return Status;
}

static NTSTATUS KexpUacPatchImage(PVOID Base, ULONG MappedSize)
{
    PIMAGE_DOS_HEADER Dos;
    PIMAGE_NT_HEADERS Nt;
    PIMAGE_SECTION_HEADER Sections;
    PIMAGE_IMPORT_DESCRIPTOR Descriptor;
    PIMAGE_THUNK_DATA Names, Addresses;
    PIMAGE_IMPORT_BY_NAME Import;
    PVOID Native;
    ULONG Size, DirectoryRva, DirectorySize, DescriptorIndex, ThunkIndex;
    ULONG NameRva, Index, NameLength, SlotRva, SectionRva;
    SIZE_T Offset;
    NTSTATUS Status;

    Status = STATUS_SUCCESS;
    if (!Base || MappedSize < sizeof(IMAGE_DOS_HEADER)) return Status;
    Dos = (PIMAGE_DOS_HEADER) Base;
    if (Dos->e_magic != IMAGE_DOS_SIGNATURE || Dos->e_lfanew <= 0 ||
        !KexpUacRange((ULONG) Dos->e_lfanew, sizeof(IMAGE_NT_HEADERS), MappedSize)) return Status;
    Nt = (PIMAGE_NT_HEADERS) ((PBYTE) Base + Dos->e_lfanew);
    if (Nt->Signature != IMAGE_NT_SIGNATURE ||
        Nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
        Nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) ||
        Nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT) return Status;
    Size = Nt->OptionalHeader.SizeOfImage;
    if (!Size || Size > MappedSize) return Status;
    Offset = (SIZE_T) Dos->e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) +
        Nt->FileHeader.SizeOfOptionalHeader;
    if (Offset > 0xFFFFFFFFUL || !KexpUacRange((ULONG) Offset,
        (SIZE_T) Nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER), Size)) return Status;
    SectionRva = (ULONG) Offset;
    Sections = (PIMAGE_SECTION_HEADER) ((PBYTE) Base + SectionRva);
    DirectoryRva = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    DirectorySize = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
    if (!DirectoryRva || !KexpUacRange(DirectoryRva, DirectorySize, Size)) return Status;
    Descriptor = (PIMAGE_IMPORT_DESCRIPTOR) ((PBYTE) Base + DirectoryRva);
    for (DescriptorIndex = 0;
        DescriptorIndex < DirectorySize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        ++DescriptorIndex, ++Descriptor) {
        if (!Descriptor->Name) break;
        if (!Descriptor->OriginalFirstThunk || !Descriptor->FirstThunk ||
            !KexpUacRange(Descriptor->OriginalFirstThunk, sizeof(IMAGE_THUNK_DATA), Size) ||
            !KexpUacRange(Descriptor->FirstThunk, sizeof(IMAGE_THUNK_DATA), Size)) continue;
        Names = (PIMAGE_THUNK_DATA) ((PBYTE) Base + Descriptor->OriginalFirstThunk);
        Addresses = (PIMAGE_THUNK_DATA) ((PBYTE) Base + Descriptor->FirstThunk);
        for (ThunkIndex = 0; ; ++ThunkIndex) {
            Offset = (SIZE_T) ThunkIndex * sizeof(IMAGE_THUNK_DATA);
            if (Offset > Size || !KexpUacRange(Descriptor->OriginalFirstThunk,
                Offset + sizeof(IMAGE_THUNK_DATA), Size) ||
                !KexpUacRange(Descriptor->FirstThunk,
                Offset + sizeof(IMAGE_THUNK_DATA), Size)) break;
            if (!Names[ThunkIndex].u1.AddressOfData) break;
            if (Names[ThunkIndex].u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            if (Names[ThunkIndex].u1.AddressOfData > 0xFFFFFFFFUL) continue;
            NameRva = (ULONG) Names[ThunkIndex].u1.AddressOfData;
            if (!KexpUacRange(NameRva, FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) + 1, Size)) continue;
            Import = (PIMAGE_IMPORT_BY_NAME) ((PBYTE) Base + NameRva);
            for (NameLength = 0; NameLength < 64 &&
                KexpUacRange(NameRva, FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) +
                    NameLength + 1, Size); ++NameLength) {
                if (!Import->Name[NameLength]) break;
            }
            if (NameLength == 64 || !KexpUacRange(NameRva,
                FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) + NameLength + 1, Size) ||
                Import->Name[NameLength]) continue;
            SlotRva = Descriptor->FirstThunk + (ULONG) Offset;
            if (!KexpUacDataSlot(Sections, Nt->FileHeader.NumberOfSections, SlotRva, Size)) continue;
            for (Index = 0; Index < ARRAYSIZE(KexpUacRoutes); ++Index) {
                Native = KexpUacRoutes[Index].Native;
                if (Native && KexpUacNameEqual((PCSTR) Import->Name, KexpUacRoutes[Index].Name)) {
                    Status = KexpUacPatchSlot((PVOID *) &Addresses[ThunkIndex].u1.Function,
                        Native, KexpUacRoutes[Index].Replacement);
                    if (!NT_SUCCESS(Status)) return Status;
                    break;
                }
            }
        }
    }
    return Status;
}

KEXAPI NTSTATUS NTAPI KexUacPatchLoadedModules(VOID)
{
    NTSTATUS Status;
    PVOID Cookie;
    PLIST_ENTRY Head, Link;
    PLDR_DATA_TABLE_ENTRY Entry;
    ULONG Count, LastError;

    LastError = NtCurrentTeb()->LastErrorValue;
    Cookie = NULL;
    Status = LdrLockLoaderLock(0, NULL, &Cookie);
    if (!NT_SUCCESS(Status)) {
        NtCurrentTeb()->LastErrorValue = LastError;
        return Status;
    }
    __try {
        KexpUacResolveNative();
        RtlAcquireSRWLockExclusive(&KexpUacPatchLock);
        __try {
            Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
            for (Link = Head->Flink, Count = 0; Link != Head && Count < 4096;
                Link = Link->Flink, ++Count) {
                Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                if (KexpUacPathWithin(&Entry->FullDllName, &KexData->WinDir) ||
                    KexpUacPathWithin(&Entry->FullDllName, &KexData->KexDir)) continue;
                __try {
                    Status = KexpUacPatchImage(Entry->DllBase, Entry->SizeOfImage);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Status = GetExceptionCode();
                }
                if (!NT_SUCCESS(Status)) break;
            }
        } __finally {
            RtlReleaseSRWLockExclusive(&KexpUacPatchLock);
        }
    } __finally {
        LdrUnlockLoaderLock(0, Cookie);
        NtCurrentTeb()->LastErrorValue = LastError;
    }
    return Status;
}

KEXAPI PVOID NTAPI KexUacGetReplacement(PVOID NativeAddress, PCSTR ApiName)
{
    ULONG Index, LastError;
    PVOID Cookie, Result;
    LastError = NtCurrentTeb()->LastErrorValue;
    Result = NULL;
    if (!NativeAddress || !ApiName || (ULONG_PTR) ApiName <= 0xFFFF) return NULL;
    for (Index = 0; Index < ARRAYSIZE(KexpUacRoutes); ++Index) {
        if (!KexpUacNameEqual(ApiName, KexpUacRoutes[Index].Name)) continue;
        Cookie = NULL;
        if (NT_SUCCESS(LdrLockLoaderLock(0, NULL, &Cookie))) {
            KexpUacResolveNative();
            if (NativeAddress == KexpUacRoutes[Index].Native)
                Result = KexpUacRoutes[Index].Replacement;
            LdrUnlockLoaderLock(0, Cookie);
        }
        break;
    }
    NtCurrentTeb()->LastErrorValue = LastError;
    return Result;
}

// This takes only the loader lock, never PatchLock. Keep the acquired provider
// reference for process lifetime, so a returned function cannot be unloaded.
static PVOID KexpUacShellTarget(ULONG Index)
{
    PVOID Cookie, Module, Procedure, Result;
    UNICODE_STRING Path;
    ANSI_STRING Name;
    WCHAR Buffer[MAX_PATH];
    ULONG FunctionIndex, LastError;
    NTSTATUS Status;

    LastError = NtCurrentTeb()->LastErrorValue;
    Result = KexpUacRoutes[Index].Native;
    Cookie = NULL;
    Status = LdrLockLoaderLock(0, NULL, &Cookie);
    if (!NT_SUCCESS(Status)) {
        NtCurrentTeb()->LastErrorValue = LastError;
        return Result;
    }
    __try {
        if (!KexpUacProvider && !KexpUacProviderLoading) {
            KexpUacProviderLoading = TRUE;
            __try {
                RtlInitEmptyUnicodeString(&Path, Buffer, sizeof(Buffer));
                Status = RtlAppendUnicodeStringToString(&Path, &KexData->Kex3264DirPath);
                if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(&Path, L"\\KxUac.dll");
                Module = NULL;
                if (NT_SUCCESS(Status)) Status = LdrLoadDll(NULL, NULL, &Path, &Module);
                if (NT_SUCCESS(Status) && Module) {
                    KexpUacProvider = Module;
                    for (FunctionIndex = 0; FunctionIndex < 4; ++FunctionIndex) {
                        RtlInitAnsiString(&Name, KexpUacRoutes[FunctionIndex].Name);
                        Procedure = NULL;
                        if (NT_SUCCESS(LdrGetProcedureAddress(Module, &Name, 0, &Procedure)))
                            KexpUacProviderFunctions[FunctionIndex] = Procedure;
                    }
                }
            } __finally {
                KexpUacProviderLoading = FALSE;
            }
        }
        if (KexpUacProviderFunctions[Index]) Result = KexpUacProviderFunctions[Index];
    } __finally {
        LdrUnlockLoaderLock(0, Cookie);
        NtCurrentTeb()->LastErrorValue = LastError;
    }
    return Result;
}

static HINSTANCE WINAPI KexpUacShellA(HWND Window, PCSTR Verb, PCSTR File,
    PCSTR Parameters, PCSTR Directory, INT Show)
{
    KEX_UAC_SHELL_A Function;
    Function = (KEX_UAC_SHELL_A) KexpUacShellTarget(0);
    return Function(Window, Verb, File, Parameters, Directory, Show);
}

static HINSTANCE WINAPI KexpUacShellW(HWND Window, PCWSTR Verb, PCWSTR File,
    PCWSTR Parameters, PCWSTR Directory, INT Show)
{
    KEX_UAC_SHELL_W Function;
    Function = (KEX_UAC_SHELL_W) KexpUacShellTarget(1);
    return Function(Window, Verb, File, Parameters, Directory, Show);
}

static BOOL WINAPI KexpUacShellExA(PVOID Information)
{
    KEX_UAC_SHELL_EX Function;
    Function = (KEX_UAC_SHELL_EX) KexpUacShellTarget(2);
    return Function(Information);
}

static BOOL WINAPI KexpUacShellExW(PVOID Information)
{
    KEX_UAC_SHELL_EX Function;
    Function = (KEX_UAC_SHELL_EX) KexpUacShellTarget(3);
    return Function(Information);
}

static HMODULE WINAPI KexpUacLoadA(PCSTR Name)
{
    HMODULE Module;
    ULONG LastError;
    Module = ((KEX_UAC_LOAD_A) KexpUacRoutes[4].Native)(Name);
    LastError = NtCurrentTeb()->LastErrorValue;
    KexUacPatchLoadedModules();
    NtCurrentTeb()->LastErrorValue = LastError;
    return Module;
}

static HMODULE WINAPI KexpUacLoadW(PCWSTR Name)
{
    HMODULE Module;
    ULONG LastError;
    Module = ((KEX_UAC_LOAD_W) KexpUacRoutes[5].Native)(Name);
    LastError = NtCurrentTeb()->LastErrorValue;
    KexUacPatchLoadedModules();
    NtCurrentTeb()->LastErrorValue = LastError;
    return Module;
}

static HMODULE WINAPI KexpUacLoadExA(PCSTR Name, HANDLE File, ULONG Flags)
{
    HMODULE Module;
    ULONG LastError;
    Module = ((KEX_UAC_LOAD_EX_A) KexpUacRoutes[6].Native)(Name, File, Flags);
    LastError = NtCurrentTeb()->LastErrorValue;
    KexUacPatchLoadedModules();
    NtCurrentTeb()->LastErrorValue = LastError;
    return Module;
}

static HMODULE WINAPI KexpUacLoadExW(PCWSTR Name, HANDLE File, ULONG Flags)
{
    HMODULE Module;
    ULONG LastError;
    Module = ((KEX_UAC_LOAD_EX_W) KexpUacRoutes[7].Native)(Name, File, Flags);
    LastError = NtCurrentTeb()->LastErrorValue;
    KexUacPatchLoadedModules();
    NtCurrentTeb()->LastErrorValue = LastError;
    return Module;
}
