/*
 * Windows 7 COM activation of explicitly configured per-user GoogleUpdater.
 * RPCSS cannot inherit the caller's VxKex process-creation compatibility. Only
 * the tested updater 157 registration is eligible after native error 193.
 * Start its unchanged registered server with the caller's token, then obtain
 * the requested object through real native COM activation.
 */
#pragma once
#include <KxCfgHlp.h>
#include <ShlObj.h>
#include <ShellApi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

STATIC volatile LONG KxpUpdaterFallbackBusy = 0;

STATIC BOOLEAN KxpUpdaterMediumUnimpersonated(VOID)
{
    HANDLE Token;
    HANDLE ThreadToken;
    TOKEN_ELEVATION Elevation;
    UCHAR IntegrityBuffer[512];
    PTOKEN_MANDATORY_LABEL Label;
    ULONG Size;
    ULONG Rid;
    UCHAR Count;
    BOOLEAN Allowed;

    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &ThreadToken)) {
        CloseHandle(ThreadToken);
        return FALSE;
    }
    if (GetLastError() != ERROR_NO_TOKEN) {
        return FALSE;
    }
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token)) {
        return FALSE;
    }
    Allowed = FALSE;
    if (GetTokenInformation(Token, TokenElevation, &Elevation,
            sizeof(Elevation), &Size) && !Elevation.TokenIsElevated &&
        GetTokenInformation(Token, TokenIntegrityLevel, IntegrityBuffer,
            sizeof(IntegrityBuffer), &Size)) {

        Label = (PTOKEN_MANDATORY_LABEL) IntegrityBuffer;
        Count = *GetSidSubAuthorityCount(Label->Label.Sid);
        if (Count != 0) {
            Rid = *GetSidSubAuthority(Label->Label.Sid, Count - 1);
            Allowed = (Rid == SECURITY_MANDATORY_MEDIUM_RID);
        }
    }
    CloseHandle(Token);
    return Allowed;
}

STATIC BOOLEAN KxpUpdaterValueAbsent(HKEY Key, PCWSTR Name)
{
    ULONG Result;
    Result = RegQueryValueExW(Key, Name, NULL, NULL, NULL, NULL);
    return Result == ERROR_FILE_NOT_FOUND;
}

STATIC BOOLEAN KxpUpdaterReadString(HKEY Key, PCWSTR Name,
    PWSTR Buffer, ULONG BufferCch, BOOLEAN Required)
{
    ULONG Type;
    ULONG Bytes;
    ULONG Result;

    Buffer[0] = L'\0';
    Bytes = BufferCch * sizeof(WCHAR);
    Result = RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE) Buffer, &Bytes);
    if (!Required && Result == ERROR_FILE_NOT_FOUND) {
        return TRUE;
    }
    if (Result != ERROR_SUCCESS || Type != REG_SZ ||
        Bytes < sizeof(WCHAR) || Bytes > BufferCch * sizeof(WCHAR) ||
        Bytes % sizeof(WCHAR) != 0 ||
        Buffer[Bytes / sizeof(WCHAR) - 1] != L'\0') {

        Buffer[0] = L'\0';
        return FALSE;
    }
    /* Do not silently accept registry data containing hidden command tails. */
    return (wcslen(Buffer) + 1) * sizeof(WCHAR) == Bytes;
}

STATIC BOOLEAN KxpUpdaterPlainClass(HKEY Key)
{
    HKEY Redirect;
    ULONG Result;

    if (!KxpUpdaterValueAbsent(Key, L"AppID") ||
        !KxpUpdaterValueAbsent(Key, L"LocalService") ||
        !KxpUpdaterValueAbsent(Key, L"RunAs")) {
        return FALSE;
    }
    Result = RegOpenKeyExW(Key, L"TreatAs", 0, KEY_READ, &Redirect);
    if (Result == ERROR_SUCCESS) {
        RegCloseKey(Redirect);
        return FALSE;
    }
    if (Result != ERROR_FILE_NOT_FOUND) {
        return FALSE;
    }
    Result = RegOpenKeyExW(Key, L"AutoTreatAs", 0, KEY_READ, &Redirect);
    if (Result == ERROR_SUCCESS) {
        RegCloseKey(Redirect);
        return FALSE;
    }
    return Result == ERROR_FILE_NOT_FOUND;
}

STATIC BOOLEAN KxpUpdaterExplicitConfiguration(PCWSTR Exe)
{
    KXCFG_PROGRAM_CONFIGURATION Configuration;
    UNICODE_STRING Image;
    NTSTATUS Status;
    HKEY Options;
    WCHAR Filter[MAX_PATH];
    BOOLEAN Allowed;

    if (!KxCfgGetConfiguration(Exe, &Configuration) ||
        !Configuration.Enabled || Configuration.DisableForChild ||
        Configuration.DisableAppSpecificHacks ||
        Configuration.WinVerSpoof != WinVerSpoofWin10 ||
        Configuration.StrongSpoofOptions != 0) {
        return FALSE;
    }
    RtlInitUnicodeString(&Image, Exe);
    Status = LdrOpenImageFileOptionsKey(&Image, FALSE, (PHANDLE) &Options);
    if (!NT_SUCCESS(Status)) {
        return FALSE;
    }
    Allowed = KxpUpdaterReadString(Options, L"FilterFullPath",
        Filter, ARRAYSIZE(Filter), TRUE) &&
        StringEqualI(Filter, Exe) &&
        KxpUpdaterValueAbsent(Options, L"Debugger");
    RegCloseKey(Options);
    return Allowed;
}

STATIC HRESULT KxpTryConfiguredUserUpdater(
    REFCLSID RefCLSID, LPUNKNOWN OuterUnknown, ULONG ClassContext,
    REFIID RefIID, PPVOID Instance, HRESULT OriginalResult)
{
    STATIC CONST CLSID InternalUser =
        {0x6e1e5c99,0xbc66,0x5838,{0x92,0x6c,0xb4,0x7f,0xc5,0xa1,0xb4,0x4a}};
    STATIC CONST CLSID ActiveUser =
        {0x547e9aef,0x8043,0x5d26,{0x87,0x9f,0x01,0xe7,0x66,0x41,0x92,0xdc}};
    PCWSTR ClassText;
    PCWSTR ServiceArgument;
    HKEY ClassKey;
    HKEY LocalKey;
    HKEY MachineKey;
    HANDLE File;
    WCHAR AppData[MAX_PATH];
    WCHAR Exe[MAX_PATH];
    WCHAR ClassPath[128];
    WCHAR Registered[1024];
    WCHAR ServerExecutable[MAX_PATH];
    WCHAR FinalPath[MAX_PATH + 8];
    WCHAR Directory[MAX_PATH];
    WCHAR Command[1100];
    WCHAR Drive[4];
    PWSTR Slash;
    PPWSTR Arguments;
    INT ArgumentCount;
    ULONG Result;
    ULONG Attributes;
    ULONG FinalCch;
    ULONG OriginalLastError;
    ULONG Started;
    ULONG SpawnError;
    ULONG Stage;
    BOOLEAN BusyHeld;
    BOOLEAN Spawned;
    STARTUPINFOW Startup;
    PROCESS_INFORMATION Process;
    HRESULT RetryResult;
    HRESULT ReturnResult;
    PVOID Object;

    if (OriginalResult != HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT) ||
        OuterUnknown != NULL || ClassContext != CLSCTX_LOCAL_SERVER ||
        Instance == NULL || KexData == NULL ||
        KexData->IfeoParameters.DisableForChild ||
        KexData->IfeoParameters.DisableAppSpecific ||
        KexData->IfeoParameters.StrongVersionSpoof != 0 ||
        SharedUserData->NtMajorVersion != 6 ||
        SharedUserData->NtMinorVersion != 1) {
        return OriginalResult;
    }
    if (IsEqualGUID(RefCLSID, &InternalUser)) {
        ClassText = L"{6E1E5C99-BC66-5838-926C-B47FC5A1B44A}";
        ServiceArgument = L"--service=update-internal";
    } else if (IsEqualGUID(RefCLSID, &ActiveUser)) {
        ClassText = L"{547E9AEF-8043-5D26-879F-01E7664192DC}";
        ServiceArgument = L"--service=update";
    } else {
        return OriginalResult;
    }
    OriginalLastError = GetLastError();
    ReturnResult = OriginalResult;
    RetryResult = OriginalResult;
    ClassKey = LocalKey = MachineKey = NULL;
    File = INVALID_HANDLE_VALUE;
    Arguments = NULL;
    BusyHeld = Spawned = FALSE;
    SpawnError = 0;
    Stage = 1;
    RtlZeroMemory(&Process, sizeof(Process));
    if (!KxpUpdaterMediumUnimpersonated() ||
        InterlockedCompareExchange(&KxpUpdaterFallbackBusy, 1, 0) != 0) {
        goto Cleanup;
    }
    BusyHeld = TRUE;
    Stage = 2;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL,
            SHGFP_TYPE_CURRENT, AppData)) ||
        FAILED(StringCchPrintfW(Exe, ARRAYSIZE(Exe),
            L"%s\\Google\\GoogleUpdater\\157.0.8089.0\\updater.exe", AppData)) ||
        FAILED(StringCchPrintfW(ClassPath, ARRAYSIZE(ClassPath),
            L"Software\\Classes\\CLSID\\%s", ClassText))) {
        goto Cleanup;
    }
    if (!((Exe[0] >= L'A' && Exe[0] <= L'Z') ||
          (Exe[0] >= L'a' && Exe[0] <= L'z')) ||
        Exe[1] != L':' || Exe[2] != L'\\') {
        goto Cleanup;
    }
    Drive[0] = Exe[0]; Drive[1] = L':'; Drive[2] = L'\\'; Drive[3] = L'\0';
    if (GetDriveTypeW(Drive) != DRIVE_FIXED) {
        goto Cleanup;
    }
    Stage = 3;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, ClassPath, 0,
            KEY_READ | KEY_WOW64_32KEY, &ClassKey) != ERROR_SUCCESS ||
        !KxpUpdaterPlainClass(ClassKey)) {
        goto Cleanup;
    }
    Result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, ClassPath, 0,
        KEY_READ | KEY_WOW64_32KEY, &MachineKey);
    if (Result != ERROR_FILE_NOT_FOUND &&
        (Result != ERROR_SUCCESS || !KxpUpdaterPlainClass(MachineKey))) {
        goto Cleanup;
    }
    if (RegOpenKeyExW(ClassKey, L"LocalServer32", 0, KEY_READ,
            &LocalKey) != ERROR_SUCCESS ||
        !KxpUpdaterReadString(LocalKey, NULL, Registered,
            ARRAYSIZE(Registered), TRUE) ||
        !KxpUpdaterReadString(LocalKey, L"ServerExecutable", ServerExecutable,
            ARRAYSIZE(ServerExecutable), FALSE) ||
        (ServerExecutable[0] != L'\0' && !StringEqualI(ServerExecutable, Exe))) {
        goto Cleanup;
    }
    Stage = 4;
    Arguments = CommandLineToArgvW(Registered, &ArgumentCount);
    if (Arguments == NULL || ArgumentCount != 3 ||
        !StringEqualI(Arguments[0], Exe) ||
        !StringEqual(Arguments[1], L"--server") ||
        !StringEqual(Arguments[2], ServiceArgument) ||
        !KxpUpdaterExplicitConfiguration(Exe)) {
        goto Cleanup;
    }
    Stage = 5;
    Attributes = GetFileAttributesW(Exe);
    if (Attributes == INVALID_FILE_ATTRIBUTES ||
        (Attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        goto Cleanup;
    }
    File = CreateFileW(Exe, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE) {
        goto Cleanup;
    }
    FinalCch = GetFinalPathNameByHandleW(File, FinalPath,
        ARRAYSIZE(FinalPath), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (FinalCch == 0 || FinalCch >= ARRAYSIZE(FinalPath) ||
        (!StringEqualI(FinalPath, Exe) &&
         !(StringBeginsWith(FinalPath, L"\\\\?\\") &&
           StringEqualI(FinalPath + 4, Exe)))) {
        goto Cleanup;
    }
    Stage = 6;
    Object = NULL;
    RetryResult = CoCreateInstance(RefCLSID, OuterUnknown, ClassContext,
        RefIID, &Object);
    if (SUCCEEDED(RetryResult) && Object != NULL) {
        *Instance = Object;
        ReturnResult = RetryResult;
        goto Cleanup;
    }
    if (RetryResult != OriginalResult) {
        goto Cleanup;
    }
    if (FAILED(StringCchCopyW(Directory, ARRAYSIZE(Directory), Exe)) ||
        FAILED(StringCchPrintfW(Command, ARRAYSIZE(Command),
            L"%s -Embedding", Registered))) {
        goto Cleanup;
    }
    Slash = wcsrchr(Directory, L'\\');
    if (Slash == NULL) {
        goto Cleanup;
    }
    *Slash = L'\0';
    RtlZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    Stage = 7;
    Spawned = (BOOLEAN) CreateProcessW(Exe, Command, NULL, NULL, FALSE, 0,
        NULL, Directory, &Startup, &Process);
    if (!Spawned) {
        SpawnError = GetLastError();
        goto Cleanup;
    }
    CloseHandle(Process.hThread);
    Process.hThread = NULL;
    Started = GetTickCount();
    Stage = 8;
    do {
        Object = NULL;
        RetryResult = CoCreateInstance(RefCLSID, OuterUnknown, ClassContext,
            RefIID, &Object);
        if (SUCCEEDED(RetryResult) && Object != NULL) {
            *Instance = Object;
            ReturnResult = RetryResult;
            break;
        }
        if (RetryResult != OriginalResult ||
            WaitForSingleObject(Process.hProcess, 0) == WAIT_OBJECT_0) {
            break;
        }
        Sleep(100);
    } while ((ULONG)(GetTickCount() - Started) < 5000);

Cleanup:
    if (Arguments != NULL) LocalFree(Arguments);
    if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
    if (LocalKey != NULL) RegCloseKey(LocalKey);
    if (ClassKey != NULL) RegCloseKey(ClassKey);
    if (MachineKey != NULL) RegCloseKey(MachineKey);
    if (Process.hThread != NULL) CloseHandle(Process.hThread);
    if (Process.hProcess != NULL) CloseHandle(Process.hProcess);
    if (BusyHeld) InterlockedExchange(&KxpUpdaterFallbackBusy, 0);
    KexLogInformationEvent(
        L"Configured per-user updater COM fallback.\r\n"
        L"Stage: %lu, spawned: %u, PID: %lu, spawn error: %lu\r\n"
        L"Original HRESULT: 0x%08lx, retry HRESULT: 0x%08lx, returned: 0x%08lx",
        Stage, Spawned, Process.dwProcessId, SpawnError,
        OriginalResult, RetryResult, ReturnResult);
    if (FAILED(ReturnResult)) {
        *Instance = NULL;
        SetLastError(OriginalLastError);
    }
    return ReturnResult;
}
