// Same-user UAC helper. Consent is provided only by normal Windows runas.
// No target registry configuration, remote handles or token changes are used.
#ifdef KEX_UAC_TEST
#include "uac_test_shim.h"
#else
#include "buildcfg.h"
#include <KexUac.h>
#include <ShellApi.h>
#pragma comment(lib, "shell32.lib")
#endif
#include <KexUacIo.h>

static BOOLEAN KxuNonceValid(PCWSTR Nonce)
{
    ULONG Index;
    for (Index = 0; Index < KXU_NONCE_CCH - 1; ++Index) {
        if (!((Nonce[Index] >= L'0' && Nonce[Index] <= L'9') ||
            (Nonce[Index] >= L'a' && Nonce[Index] <= L'f'))) return FALSE;
    }
    return Nonce[KXU_NONCE_CCH - 1] == 0;
}

static BOOLEAN KxuParsePid(PCWSTR Text, PULONG Pid)
{
    ULONG Value, Digit, Index;
    Value = 0;
    if (!Text[0]) return FALSE;
    for (Index = 0; Text[Index]; ++Index) {
        if (Index >= 10 || Text[Index] < L'0' || Text[Index] > L'9') return FALSE;
        Digit = Text[Index] - L'0';
        if (Value > (MAXDWORD - Digit) / 10) return FALSE;
        Value = Value * 10 + Digit;
    }
    *Pid = Value;
    return Value != 0;
}

static BOOLEAN KxuParseCreationTime(PCWSTR Text, PULONGLONG Time)
{
    ULONG Index, Digit;
    ULONGLONG Value;
    Value = 0;
    for (Index = 0; Index < 16; ++Index) {
        if (Text[Index] >= L'0' && Text[Index] <= L'9') Digit = Text[Index] - L'0';
        else if (Text[Index] >= L'a' && Text[Index] <= L'f') Digit = Text[Index] - L'a' + 10;
        else if (Text[Index] >= L'A' && Text[Index] <= L'F') Digit = Text[Index] - L'A' + 10;
        else return FALSE;
        Value = (Value << 4) | Digit;
    }
    *Time = Value;
    return Text[16] == 0 && Value != 0;
}

static DWORD KxuProcessTime(HANDLE Process, PULONGLONG Time)
{
    FILETIME Creation, Exit, Kernel, User;
    if (!GetProcessTimes(Process, &Creation, &Exit, &Kernel, &User)) return GetLastError();
    *Time = KxuFileTime(Creation);
    return ERROR_SUCCESS;
}

static DWORD KxuTokenData(HANDLE Token, TOKEN_INFORMATION_CLASS Class,
    PVOID *Data)
{
    ULONG Required, Error;
    *Data = NULL;
    Required = 0;
    GetTokenInformation(Token, Class, NULL, 0, &Required);
    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER || !Required || Required > 65536) {
        return !Error || Error == ERROR_INSUFFICIENT_BUFFER ? ERROR_INVALID_DATA : Error;
    }
    *Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required);
    if (!*Data) return ERROR_NOT_ENOUGH_MEMORY;
    if (!GetTokenInformation(Token, Class, *Data, Required, &Required)) {
        Error = GetLastError();
        HeapFree(GetProcessHeap(), 0, *Data);
        *Data = NULL;
        return Error;
    }
    return ERROR_SUCCESS;
}

static DWORD KxuValidateCaller(HANDLE Caller)
{
    HANDLE SelfToken, CallerToken;
    PTOKEN_USER SelfUser, CallerUser;
    PTOKEN_MANDATORY_LABEL Integrity;
    TOKEN_ELEVATION Elevation;
    ULONG SelfSession, CallerSession, Required, Error;
    UCHAR Count;

    SelfToken = CallerToken = NULL;
    SelfUser = CallerUser = NULL;
    Integrity = NULL;
    Error = ERROR_ACCESS_DENIED;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &SelfToken) ||
        !OpenProcessToken(Caller, TOKEN_QUERY, &CallerToken)) {
        Error = GetLastError();
        goto Exit;
    }
    Required = 0;
    if (!GetTokenInformation(SelfToken, TokenElevation, &Elevation, sizeof(Elevation), &Required) ||
        !GetTokenInformation(SelfToken, TokenSessionId, &SelfSession, sizeof(SelfSession), &Required) ||
        !GetTokenInformation(CallerToken, TokenSessionId, &CallerSession, sizeof(CallerSession), &Required)) {
        Error = GetLastError();
        goto Exit;
    }
    if (!Elevation.TokenIsElevated || SelfSession != CallerSession) {
        Error = ERROR_ACCESS_DENIED;
        goto Exit;
    }
    Error = KxuTokenData(SelfToken, TokenUser, (PVOID *) &SelfUser);
    if (Error) goto Exit;
    Error = KxuTokenData(CallerToken, TokenUser, (PVOID *) &CallerUser);
    if (Error) goto Exit;
    Error = KxuTokenData(SelfToken, TokenIntegrityLevel, (PVOID *) &Integrity);
    if (Error) goto Exit;
    if (!IsValidSid(SelfUser->User.Sid) || !IsValidSid(CallerUser->User.Sid) ||
        !EqualSid(SelfUser->User.Sid, CallerUser->User.Sid) ||
        !IsValidSid(Integrity->Label.Sid)) {
        Error = ERROR_ACCESS_DENIED;
        goto Exit;
    }
    Count = *GetSidSubAuthorityCount(Integrity->Label.Sid);
    if (!Count || *GetSidSubAuthority(Integrity->Label.Sid, Count - 1) < SECURITY_MANDATORY_HIGH_RID) {
        Error = ERROR_ACCESS_DENIED;
        goto Exit;
    }
    Error = ERROR_SUCCESS;
Exit:
    if (Integrity) HeapFree(GetProcessHeap(), 0, Integrity);
    if (CallerUser) HeapFree(GetProcessHeap(), 0, CallerUser);
    if (SelfUser) HeapFree(GetProcessHeap(), 0, SelfUser);
    if (CallerToken) CloseHandle(CallerToken);
    if (SelfToken) CloseHandle(SelfToken);
    return Error;
}

static DWORD KxuPinImage(CONST KXU_REQUEST *Request, PHANDLE Image)
{
    BY_HANDLE_FILE_INFORMATION Information;
    WCHAR Root[4];
    DWORD Attributes, Type, Error;

    *Image = INVALID_HANDLE_VALUE;
    Root[0] = Request->File[0]; Root[1] = L':'; Root[2] = L'\\'; Root[3] = 0;
    Type = GetDriveTypeW(Root);
    if (Type == DRIVE_REMOTE || Type == DRIVE_UNKNOWN || Type == DRIVE_NO_ROOT_DIR) {
        return ERROR_NOT_SUPPORTED;
    }
    Root[0] = Request->Directory[0];
    Type = GetDriveTypeW(Root);
    if (Type == DRIVE_REMOTE || Type == DRIVE_UNKNOWN || Type == DRIVE_NO_ROOT_DIR) {
        return ERROR_NOT_SUPPORTED;
    }
    Attributes = GetFileAttributesW(Request->Directory);
    if (Attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (!(Attributes & FILE_ATTRIBUTE_DIRECTORY) || (Attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return ERROR_NOT_SUPPORTED;
    }
    *Image = CreateFileW(Request->File, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (*Image == INVALID_HANDLE_VALUE) return GetLastError();
    if (!GetFileInformationByHandle(*Image, &Information)) {
        Error = GetLastError();
        goto Failure;
    }
    if ((Information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        Information.dwVolumeSerialNumber != Request->VolumeSerial ||
        Information.nFileIndexHigh != Request->FileIndexHigh ||
        Information.nFileIndexLow != Request->FileIndexLow ||
        Information.nFileSizeHigh != Request->FileSizeHigh ||
        Information.nFileSizeLow != Request->FileSizeLow ||
        Information.ftLastWriteTime.dwHighDateTime != Request->WriteTimeHigh ||
        Information.ftLastWriteTime.dwLowDateTime != Request->WriteTimeLow) {
        Error = ERROR_FILE_INVALID;
        goto Failure;
    }
    return ERROR_SUCCESS;
Failure:
    CloseHandle(*Image);
    *Image = INVALID_HANDLE_VALUE;
    return Error;
}

DWORD WINAPI KxuElevateMain(VOID)
{
    PWSTR *Arguments;
    INT ArgumentCount;
    ULONG CallerPid, ServerPid, Error, ChildExit, PreviousSuspendCount;
    ULONGLONG CallerTime, ActualTime, SelfTime, ChildTime;
    HANDLE Pipe, Caller, Image;
    WCHAR PipeName[128];
    PWSTR CommandLine;
    ULONG CommandCch;
    KXU_REQUEST *Request;
    KXU_MESSAGE Message;
    PKEX_PROCESS_DATA Data;
    NTSTATUS Status;
    PROCESS_INFORMATION Process;
    STARTUPINFOW Startup;
    BOOLEAN HelloSent, Resumed;

    Arguments = NULL; Request = NULL; CommandLine = NULL;
    Pipe = Image = INVALID_HANDLE_VALUE; Caller = NULL;
    ZeroMemory(&Process, sizeof(Process));
    HelloSent = Resumed = FALSE;
    Error = ERROR_INVALID_PARAMETER;
    SelfTime = 0;
    Arguments = CommandLineToArgvW(GetCommandLineW(), &ArgumentCount);
    if (!Arguments) return GetLastError();
    if (ArgumentCount != 5 || !StringEqualW(Arguments[1], L"--uac") ||
        !KxuNonceValid(Arguments[2]) || !KxuParsePid(Arguments[3], &CallerPid) ||
        !KxuParseCreationTime(Arguments[4], &CallerTime)) goto Exit;
    if (FAILED(StringCchPrintfW(PipeName, ARRAYSIZE(PipeName),
        L"%s%s", KXU_PIPE_PREFIX, Arguments[2]))) goto Exit;
    Pipe = CreateFileW(PipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
    if (Pipe == INVALID_HANDLE_VALUE) { Error = GetLastError(); goto Exit; }
    if (!GetNamedPipeServerProcessId(Pipe, &ServerPid)) { Error = GetLastError(); goto Exit; }
    if (ServerPid != CallerPid) { Error = ERROR_ACCESS_DENIED; goto Exit; }
    Caller = OpenProcess(KXU_TARGET_ACCESS, FALSE, CallerPid);
    if (!Caller) { Error = GetLastError(); goto Exit; }
    Error = KxuProcessTime(Caller, &ActualTime);
    if (Error) goto Exit;
    if (ActualTime != CallerTime) { Error = ERROR_ACCESS_DENIED; goto Exit; }
    if (WaitForSingleObject(Caller, 0) != WAIT_TIMEOUT) { Error = ERROR_PROCESS_ABORTED; goto Exit; }
    Error = KxuValidateCaller(Caller);
    if (Error) goto Exit;
    Error = KxuProcessTime(GetCurrentProcess(), &SelfTime);
    if (Error) goto Exit;
    ZeroMemory(&Message, sizeof(Message));
    KxuHeader(&Message.Header, KxuHello, sizeof(Message));
    Message.Pid = GetCurrentProcessId(); Message.CreationTime = SelfTime;
    Error = KxuWrite(Pipe, &Message, sizeof(Message));
    if (Error) goto Exit;
    HelloSent = TRUE;
    Request = (KXU_REQUEST *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Request));
    if (!Request) { Error = ERROR_NOT_ENOUGH_MEMORY; goto Exit; }
    Error = KxuRead(Pipe, Request, sizeof(*Request));
    if (Error) goto Exit;
    Error = KxuValidateRequest(Request, CallerPid, CallerTime);
    if (Error) goto Exit;
    Error = KxuPinImage(Request, &Image);
    if (Error) goto Exit;
    Status = KexDataInitialize(&Data);
    if (!NT_SUCCESS(Status)) { Error = RtlNtStatusToDosError(Status); goto Exit; }
    Data->IfeoParameters = Request->Parameters;
    Status = KexInitializePropagation();
    if (!NT_SUCCESS(Status)) { Error = RtlNtStatusToDosError(Status); goto Exit; }
    Status = KexPatchCpiwSubsystemVersionCheck();
    if (!NT_SUCCESS(Status) && Status != STATUS_ALREADY_INITIALIZED) {
        Error = RtlNtStatusToDosError(Status); goto Exit;
    }
    CommandCch = Request->FileCch + Request->ArgumentsCch + 2;
    CommandLine = (PWSTR) HeapAlloc(GetProcessHeap(), 0, CommandCch * sizeof(WCHAR));
    if (!CommandLine) { Error = ERROR_NOT_ENOUGH_MEMORY; goto Exit; }
    if (FAILED(StringCchPrintfW(CommandLine, CommandCch, L"\"%s\" %s",
        Request->File, Request->Arguments))) { Error = ERROR_INVALID_PARAMETER; goto Exit; }
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    Startup.dwFlags = STARTF_USESHOWWINDOW;
    Startup.wShowWindow = (WORD) Request->ShowWindow;
    if (!CreateProcessW(Request->File, CommandLine, NULL, NULL, FALSE,
        CREATE_SUSPENDED, NULL, Request->Directory, &Startup, &Process)) {
        Error = GetLastError(); goto Exit;
    }
    Error = KxuProcessTime(Process.hProcess, &ChildTime);
    if (Error) goto Exit;
    ZeroMemory(&Message, sizeof(Message));
    KxuHeader(&Message.Header, KxuChildReady, sizeof(Message));
    Message.Pid = Process.dwProcessId; Message.CreationTime = ChildTime;
    Error = KxuWrite(Pipe, &Message, sizeof(Message));
    if (Error) goto Exit;
    Error = KxuRead(Pipe, &Message, sizeof(Message));
    if (Error) goto Exit;
    if (KxuValidHeader(&Message.Header, KxuAbort, sizeof(Message))) {
        Error = ERROR_CANCELLED; goto Exit;
    }
    if (!KxuValidHeader(&Message.Header, KxuAcknowledge, sizeof(Message)) ||
        Message.Error || Message.Pid != Process.dwProcessId || Message.CreationTime != ChildTime) {
        Error = ERROR_INVALID_DATA; goto Exit;
    }
    if (!GetExitCodeProcess(Process.hProcess, &ChildExit)) { Error = GetLastError(); goto Exit; }
    if (ChildExit != STILL_ACTIVE) { Error = ERROR_PROCESS_ABORTED; goto Exit; }
    PreviousSuspendCount = ResumeThread(Process.hThread);
    if (PreviousSuspendCount == MAXDWORD) { Error = GetLastError(); goto Exit; }
    if (PreviousSuspendCount != 1) {
        // Zero means someone already resumed it: never kill a running target.
        Resumed = PreviousSuspendCount == 0;
        Error = ERROR_INVALID_STATE;
        goto Exit;
    }
    Resumed = TRUE;
    ZeroMemory(&Message, sizeof(Message));
    KxuHeader(&Message.Header, KxuStarted, sizeof(Message));
    Message.Pid = Process.dwProcessId; Message.CreationTime = ChildTime;
    Error = KxuWrite(Pipe, &Message, sizeof(Message));
Exit:
    if (Process.hProcess && !Resumed) {
        TerminateProcess(Process.hProcess, Error ? Error : ERROR_PROCESS_ABORTED);
    }
    if (Error && HelloSent && Pipe != INVALID_HANDLE_VALUE) {
        ZeroMemory(&Message, sizeof(Message));
        KxuHeader(&Message.Header, KxuFailure, sizeof(Message));
        Message.Pid = GetCurrentProcessId(); Message.CreationTime = SelfTime; Message.Error = Error;
        KxuWrite(Pipe, &Message, sizeof(Message));
    }
    if (Process.hThread) CloseHandle(Process.hThread);
    if (Process.hProcess) CloseHandle(Process.hProcess);
    if (CommandLine) HeapFree(GetProcessHeap(), 0, CommandLine);
    if (Image != INVALID_HANDLE_VALUE) CloseHandle(Image);
    if (Request) HeapFree(GetProcessHeap(), 0, Request);
    if (Caller) CloseHandle(Caller);
    if (Pipe != INVALID_HANDLE_VALUE) CloseHandle(Pipe);
    if (Arguments) LocalFree(Arguments);
    return Error;
}
