// Versioned, pointer-free protocol for same-user split-token elevation.
// Win7 native APIs perform consent; no credentials or process-write handles.
#pragma once
#include <KexComm.h>
#include <KexDll.h>

#define KXU_MAGIC 0x3141434bUL
#define KXU_VERSION 1
#define KXU_TIMEOUT 30000
#define KXU_PIPE_PREFIX L"\\\\.\\pipe\\VxKexUac-"
#define KXU_NONCE_CCH 33
#define KXU_ARGUMENT_CCH 32768
#define KXU_TARGET_ACCESS (SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION)

enum {KxuHello=1,KxuRequest,KxuChildReady,KxuAcknowledge,KxuStarted,KxuAbort,KxuFailure};

#pragma pack(push, 8)
typedef struct {
    ULONG Magic, Version, Type, Size;
} KXU_HEADER;

typedef struct {
    KXU_HEADER Header;
    ULONG Pid, Error;
    ULONGLONG CreationTime;
} KXU_MESSAGE;

typedef struct {
    KXU_HEADER Header;
    ULONG CallerPid, FileCch;
    ULONGLONG CallerCreationTime;
    KEX_IFEO_PARAMETERS Parameters;
    LONG ShowWindow;
    ULONG ArgumentsCch, DirectoryCch;
    ULONG VolumeSerial, FileIndexHigh, FileIndexLow, FileSizeHigh, FileSizeLow;
    ULONG WriteTimeHigh, WriteTimeLow;
    ULONG Reserved;
    WCHAR File[MAX_PATH];
    WCHAR Directory[MAX_PATH];
    WCHAR Arguments[KXU_ARGUMENT_CCH];
} KXU_REQUEST;
#pragma pack(pop)

C_ASSERT(sizeof(KXU_HEADER) == 16);
C_ASSERT(sizeof(KXU_MESSAGE) == 32);
C_ASSERT(sizeof(KEX_IFEO_PARAMETERS) == 28);

static VOID KxuHeader(KXU_HEADER *Header, ULONG Type, ULONG Size)
{
    Header->Magic=KXU_MAGIC; Header->Version=KXU_VERSION;
    Header->Type=Type; Header->Size=Size;
}

static BOOLEAN KxuValidHeader(CONST KXU_HEADER *Header, ULONG Type, ULONG Size)
{
    return Header->Magic==KXU_MAGIC && Header->Version==KXU_VERSION &&
        Header->Type==Type && Header->Size==Size;
}

static BOOLEAN KxuValidParameters(CONST KEX_IFEO_PARAMETERS *Parameters)
{
    return Parameters && Parameters->DisableForChild==0 &&
        Parameters->DisableAppSpecific<=1 && Parameters->DisableConsoleEnhancements<=1 &&
        (ULONG)Parameters->WinVerSpoof<WinVerSpoofMax &&
        !(Parameters->StrongVersionSpoof & ~KEX_STRONGSPOOF_VALID_MASK);
}

static BOOLEAN KxuValidString(PCWSTR Text, ULONG Cch, ULONG Capacity)
{
    ULONG Index;
    if (!Cch || Cch>Capacity || Text[Cch-1]) return FALSE;
    for (Index=0; Index+1<Cch; ++Index) if (!Text[Index]) return FALSE;
    return TRUE;
}

static ULONGLONG KxuFileTime(FILETIME Time)
{
    return ((ULONGLONG)Time.dwHighDateTime<<32) | Time.dwLowDateTime;
}

static BOOLEAN KxuLocalPath(PCWSTR Path, BOOLEAN Executable)
{
    ULONG Index, Length;
    WCHAR Character;
    if (!((Path[0]>=L'A' && Path[0]<=L'Z') || (Path[0]>=L'a' && Path[0]<=L'z')) ||
        Path[1]!=L':' || Path[2]!=L'\\') return FALSE;
    for (Index=3;Path[Index];++Index) {
        Character=Path[Index];
        if (Character<32 || Character==L'"' || Character==L'%' || Character==L':' ||
            Character==L'/' || Character==L'*' || Character==L'?') return FALSE;
    }
    Length=Index;
    if (Executable && (Length<7 || Path[Length-4]!=L'.' ||
        (Path[Length-3]!=L'e' && Path[Length-3]!=L'E') ||
        (Path[Length-2]!=L'x' && Path[Length-2]!=L'X') ||
        (Path[Length-1]!=L'e' && Path[Length-1]!=L'E'))) return FALSE;
    return TRUE;
}

static DWORD KxuValidateRequest(CONST KXU_REQUEST *Request,ULONG CallerPid,ULONGLONG CallerTime)
{
    ULONG CommandCch;
    if (!KxuValidHeader(&Request->Header,KxuRequest,sizeof(*Request)) ||
        Request->CallerPid!=CallerPid || Request->CallerCreationTime!=CallerTime ||
        Request->Reserved || !KxuValidParameters(&Request->Parameters) ||
        (Request->Parameters.TlsForceEnabledProtocols & Request->Parameters.TlsForceDisabledProtocols) ||
        !KxuValidString(Request->File,Request->FileCch,ARRAYSIZE(Request->File)) ||
        !KxuValidString(Request->Directory,Request->DirectoryCch,ARRAYSIZE(Request->Directory)) ||
        !KxuValidString(Request->Arguments,Request->ArgumentsCch,ARRAYSIZE(Request->Arguments)) ||
        Request->ShowWindow<SW_HIDE || Request->ShowWindow>SW_FORCEMINIMIZE ||
        !KxuLocalPath(Request->File,TRUE) || !KxuLocalPath(Request->Directory,FALSE))
        return ERROR_INVALID_PARAMETER;
    CommandCch=Request->FileCch+Request->ArgumentsCch+2;
    return CommandCch<=32767 ? ERROR_SUCCESS : ERROR_FILENAME_EXCED_RANGE;
}
