/* Configure a stopped local executable service without starting it. */
#ifdef KEX_SERVICE_TEST
#include "service_test_shim.h"
#else
#include "buildcfg.h"
#undef NOSERVICE
#include <KexComm.h>
#include <KxCfgHlp.h>
#include <winsvc.h>
#endif

STATIC ULONG KxCfgpServiceLastError(VOID)
{
    ULONG Error;
    Error = GetLastError();
    return Error != ERROR_SUCCESS ? Error : ERROR_GEN_FAILURE;
}

STATIC ULONG KxCfgpRejectWindowsServicePath(
    PCWSTR Path,
    ULONG PathLength,
    PCWSTR Windows,
    ULONG WindowsLength)
{
    INT Comparison;

    if (PathLength < WindowsLength) {
        return ERROR_SUCCESS;
    }
    Comparison = CompareStringOrdinal(Path, WindowsLength, Windows,
        WindowsLength, TRUE);
    if (Comparison == 0) {
        return KxCfgpServiceLastError();
    }
    if (Comparison == CSTR_EQUAL &&
        (PathLength == WindowsLength || Path[WindowsLength] == L'\\' ||
         Windows[WindowsLength - 1] == L'\\')) {
        return ERROR_ACCESS_DENIED;
    }
    return ERROR_SUCCESS;
}

STATIC ULONG KxCfgpParseServiceExecutable(
    PCWSTR Command,
    PWSTR Path,
    ULONG PathCch)
{
    PCWSTR Begin;
    PCWSTR End;
    PCWSTR Cursor;
    ULONG Length;
    ULONG Index;

    if (Command == NULL || Command[0] == L'\0') {
        return ERROR_INVALID_NAME;
    }
    for (Cursor = Command; *Cursor != L'\0'; ++Cursor) {
        if (*Cursor == L'%') {
            return ERROR_INVALID_NAME;
        }
    }
    Begin = Command;
    if (*Begin == L'"') {
        ++Begin;
        for (End = Begin; *End != L'\0' && *End != L'"'; ++End) {
            NOTHING;
        }
        if (*End != L'"' ||
            (End[1] != L'\0' && End[1] != L' ' && End[1] != L'\t')) {
            return ERROR_INVALID_NAME;
        }
    } else {
        for (End = Begin; *End != L'\0' && *End != L' ' && *End != L'\t'; ++End) {
            if (*End == L'"') {
                return ERROR_INVALID_NAME;
            }
        }
    }
    Length = (ULONG)(End - Begin);
    if (Length >= PathCch) {
        return ERROR_FILENAME_EXCED_RANGE;
    }
    if (Length < 7 ||
        !((Begin[0] >= L'A' && Begin[0] <= L'Z') ||
          (Begin[0] >= L'a' && Begin[0] <= L'z')) ||
        Begin[1] != L':' || Begin[2] != L'\\' ||
        Begin[Length - 4] != L'.' ||
        (Begin[Length - 3] != L'e' && Begin[Length - 3] != L'E') ||
        (Begin[Length - 2] != L'x' && Begin[Length - 2] != L'X') ||
        (Begin[Length - 1] != L'e' && Begin[Length - 1] != L'E')) {
        return ERROR_INVALID_NAME;
    }
    for (Index = 0; Index < Length; ++Index) {
        if (Begin[Index] == L'/' || Begin[Index] == L'"' ||
            (Index != 1 && Begin[Index] == L':')) {
            return ERROR_INVALID_NAME;
        }
        Path[Index] = Begin[Index];
    }
    Path[Length] = L'\0';
    return ERROR_SUCCESS;
}

ULONG KXCFGAPI KxCfgTryInheritServiceParameters(
    PCWSTR ServiceName,
    const KEX_IFEO_PARAMETERS *Parameters,
    PBOOLEAN Created)
{
    SC_HANDLE Manager;
    SC_HANDLE Service;
    HANDLE Image;
    LPQUERY_SERVICE_CONFIGW Configuration;
    SERVICE_STATUS_PROCESS Status;
    ULONG Needed;
    ULONG ConfigurationCb;
    ULONG Result;
    ULONG Index;
    ULONG MaxChars;
    ULONG Attributes;
    ULONG Length;
    ULONG WindowsLength;
    ULONG FinalLength;
    ULONG_PTR BinaryAddress;
    ULONG_PTR BufferAddress;
    WCHAR Parsed[MAX_PATH];
    WCHAR Canonical[MAX_PATH];
    WCHAR Windows[MAX_PATH];
    WCHAR Final[MAX_PATH + 8];

    if (Created == NULL) {
        return ERROR_INVALID_PARAMETER;
    }
    *Created = FALSE;
    if (ServiceName == NULL || ServiceName[0] == L'\0' || Parameters == NULL) {
        return ERROR_INVALID_PARAMETER;
    }
    Manager = NULL;
    Service = NULL;
    Image = INVALID_HANDLE_VALUE;
    Configuration = NULL;
    Result = ERROR_SUCCESS;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    Service = OpenServiceW(Manager, ServiceName,
        SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (Service == NULL) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    Needed = 0;
    if (!QueryServiceStatusEx(Service, SC_STATUS_PROCESS_INFO,
            (PBYTE)&Status, sizeof(Status), &Needed)) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    if (Status.dwCurrentState != SERVICE_STOPPED) {
        Result = ERROR_SERVICE_ALREADY_RUNNING;
        goto Cleanup;
    }
    Needed = 0;
    if (QueryServiceConfigW(Service, NULL, 0, &Needed)) {
        Result = ERROR_INVALID_DATA;
        goto Cleanup;
    }
    Result = KxCfgpServiceLastError();
    if (Result != ERROR_INSUFFICIENT_BUFFER) {
        goto Cleanup;
    }
    if (Needed < sizeof(*Configuration) || Needed > 8192) {
        Result = ERROR_INVALID_DATA;
        goto Cleanup;
    }
    ConfigurationCb = Needed;
    Configuration = (LPQUERY_SERVICE_CONFIGW)LocalAlloc(LPTR, ConfigurationCb);
    if (Configuration == NULL) {
        Result = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    if (!QueryServiceConfigW(Service, Configuration, ConfigurationCb, &Needed)) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    if ((Configuration->dwServiceType & ~SERVICE_INTERACTIVE_PROCESS) !=
            SERVICE_WIN32_OWN_PROCESS) {
        Result = ERROR_NOT_SUPPORTED;
        goto Cleanup;
    }
    if (Configuration->dwStartType == SERVICE_DISABLED) {
        Result = ERROR_SERVICE_DISABLED;
        goto Cleanup;
    }
    BufferAddress = (ULONG_PTR)Configuration;
    BinaryAddress = (ULONG_PTR)Configuration->lpBinaryPathName;
    if (BinaryAddress < BufferAddress ||
        BinaryAddress >= BufferAddress + ConfigurationCb ||
        BinaryAddress % sizeof(WCHAR) != 0) {
        Result = ERROR_INVALID_DATA;
        goto Cleanup;
    }
    MaxChars = (ULONG)((BufferAddress + ConfigurationCb - BinaryAddress) /
        sizeof(WCHAR));
    for (Index = 0; Index < MaxChars; ++Index) {
        if (Configuration->lpBinaryPathName[Index] == L'\0') {
            break;
        }
    }
    if (Index == MaxChars) {
        Result = ERROR_INVALID_DATA;
        goto Cleanup;
    }
    Result = KxCfgpParseServiceExecutable(Configuration->lpBinaryPathName,
        Parsed, ARRAYSIZE(Parsed));
    if (Result != ERROR_SUCCESS) {
        goto Cleanup;
    }
    Length = GetFullPathNameW(Parsed, ARRAYSIZE(Canonical), Canonical, NULL);
    if (Length == 0) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    if (Length >= ARRAYSIZE(Canonical)) {
        Result = ERROR_FILENAME_EXCED_RANGE;
        goto Cleanup;
    }
    WindowsLength = GetWindowsDirectoryW(Windows, ARRAYSIZE(Windows));
    if (WindowsLength == 0) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    if (WindowsLength >= ARRAYSIZE(Windows)) {
        Result = ERROR_FILENAME_EXCED_RANGE;
        goto Cleanup;
    }
    while (WindowsLength > 3 && Windows[WindowsLength - 1] == L'\\') {
        Windows[--WindowsLength] = L'\0';
    }
    Result = KxCfgpRejectWindowsServicePath(Canonical, Length,
        Windows, WindowsLength);
    if (Result != ERROR_SUCCESS) {
        goto Cleanup;
    }
    Attributes = GetFileAttributesW(Canonical);
    if (Attributes == INVALID_FILE_ATTRIBUTES) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    if (Attributes & FILE_ATTRIBUTE_DIRECTORY) {
        Result = ERROR_DIRECTORY;
        goto Cleanup;
    }
    Image = CreateFileW(Canonical, FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Image == INVALID_HANDLE_VALUE) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    FinalLength = GetFinalPathNameByHandleW(Image, Final, ARRAYSIZE(Final),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (FinalLength == 0) {
        Result = KxCfgpServiceLastError();
        goto Cleanup;
    }
    if (FinalLength >= ARRAYSIZE(Final)) {
        Result = ERROR_FILENAME_EXCED_RANGE;
        goto Cleanup;
    }
    /* Accept only a resolved \\?\C:\... executable, never UNC or a device. */
    if (FinalLength < 11 || Final[0] != L'\\' || Final[1] != L'\\' ||
        Final[2] != L'?' || Final[3] != L'\\' ||
        !((Final[4] >= L'A' && Final[4] <= L'Z') ||
          (Final[4] >= L'a' && Final[4] <= L'z')) ||
        Final[5] != L':' || Final[6] != L'\\' ||
        Final[FinalLength - 4] != L'.' ||
        (Final[FinalLength - 3] != L'e' && Final[FinalLength - 3] != L'E') ||
        (Final[FinalLength - 2] != L'x' && Final[FinalLength - 2] != L'X') ||
        (Final[FinalLength - 1] != L'e' && Final[FinalLength - 1] != L'E')) {
        Result = ERROR_INVALID_NAME;
        goto Cleanup;
    }
    Result = KxCfgpRejectWindowsServicePath(Final + 4, FinalLength - 4,
        Windows, WindowsLength);
    if (Result != ERROR_SUCCESS) {
        goto Cleanup;
    }
    /* Keep the lexical SCM path for IFEO matching. The open handle validates
       its present target and denies file replacement during configuration.
       This does not lock every parent directory against later junction changes. */
    Result = KxCfgTryInheritConfiguration(Canonical, Parameters, Created);

Cleanup:
    if (Image != INVALID_HANDLE_VALUE) {
        CloseHandle(Image);
    }
    if (Configuration != NULL) {
        LocalFree(Configuration);
    }
    if (Service != NULL) {
        CloseServiceHandle(Service);
    }
    if (Manager != NULL) {
        CloseServiceHandle(Manager);
    }
    if (Result != ERROR_SUCCESS) {
        *Created = FALSE;
    }
    return Result;
}
