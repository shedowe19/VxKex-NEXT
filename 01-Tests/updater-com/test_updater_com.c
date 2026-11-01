#include "test_platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wctype.h>

/* Every operating-system call used by the extracted implementation is replaced.
   No COM activation, token access, registry I/O or process creation occurs. */
#define OpenThreadToken MockOpenThreadToken
#define OpenProcessToken MockOpenProcessToken
#define GetCurrentThread MockGetCurrentThread
#define GetCurrentProcess MockGetCurrentProcess
#define CloseHandle MockCloseHandle
#define GetLastError MockGetLastError
#define SetLastError MockSetLastError
#define GetTokenInformation MockGetTokenInformation
#define GetSidSubAuthorityCount MockGetSidSubAuthorityCount
#define GetSidSubAuthority MockGetSidSubAuthority
#define RegQueryValueExW MockRegQueryValueExW
#define RegOpenKeyExW MockRegOpenKeyExW
#define RegCloseKey MockRegCloseKey
#define KxCfgGetConfiguration MockKxCfgGetConfiguration
#define RtlInitUnicodeString MockRtlInitUnicodeString
#define LdrOpenImageFileOptionsKey MockLdrOpenImageFileOptionsKey
#ifdef InterlockedCompareExchange
#undef InterlockedCompareExchange
#endif
#ifdef InterlockedExchange
#undef InterlockedExchange
#endif
#define InterlockedCompareExchange MockInterlockedCompareExchange
#define InterlockedExchange MockInterlockedExchange
#define SHGetFolderPathW MockSHGetFolderPathW
#define StringCchPrintfW MockStringCchPrintfW
#define StringCchCopyW MockStringCchCopyW
#define GetDriveTypeW MockGetDriveTypeW
#define CommandLineToArgvW MockCommandLineToArgvW
#define LocalFree MockLocalFree
#define GetFileAttributesW MockGetFileAttributesW
#define CreateFileW MockCreateFileW
#define GetFinalPathNameByHandleW MockGetFinalPathNameByHandleW
#define CoCreateInstance MockCoCreateInstance
#define CreateProcessW MockCreateProcessW
#define GetTickCount MockGetTickCount
#define WaitForSingleObject MockWaitForSingleObject
#define Sleep MockSleep
#define KexLogInformationEvent MockLog
#define KexLogEvent MockLogEvent
#define EnsureCOMInitialized MockEnsureCOMInitialized
#define StringFromCLSID MockGuidString
#define StringFromIID MockGuidString
#define CoTaskMemFree MockCoTaskMemFree

#define H(n) ((HANDLE)(ULONG_PTR)(n))
#define K(n) ((HKEY)(ULONG_PTR)(n))
#define BAD HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT)

static const CLSID Internal = {0x6e1e5c99,0xbc66,0x5838,{0x92,0x6c,0xb4,0x7f,0xc5,0xa1,0xb4,0x4a}};
static const CLSID Active = {0x547e9aef,0x8043,0x5d26,{0x87,0x9f,0x01,0xe7,0x66,0x41,0x92,0xdc}};
static const IID RequestedIid = {0x12345678,0,0,{0}};
static const WCHAR ExpectedExe[] = L"C:\\Users\\Test\\AppData\\Local\\Google\\GoogleUpdater\\157.0.8089.0\\updater.exe";
static TEST_KEX_DATA Data;
static TEST_KEX_DATA *KexData = &Data;
static TEST_SHARED_DATA Shared;
static TEST_SHARED_DATA *SharedUserData = &Shared;
static KXCFG_PROGRAM_CONFIGURATION Config;
static ULONG LastError, NativeCalls, Spawns, LoggedStage, Clock, Sleeps, Checks;
static ULONG Integrity, TokenElevated, TestDriveType, Attributes, FinalPathResult;
static ULONG ClassOpenResult, MachineOpenResult, LocalOpenResult, OptionsOpenResult;
static ULONG QueryType, QueryFault, LaunchError;
static int Impersonating, ThreadTokenError, TokenOpenFail, TokenQueryFail;
static int ClassMetadata, MachineMetadata, TreatAs, AutoTreatAs, ConfigMissing, Debugger;
static int BadFilter, ExeOpenFail, BadCanonicalPath, EmbeddedTail, BadTerminator;
static int Argc, WrongArg, LaunchFails, ChildExited, NullRealObject;
static int FailAppData, FailPrintf, FailCopy, FinalPathZero;
static int Resources[16], ArgvHeld, Initialized;
static HRESULT NativeResults[8];
static unsigned int NativeResultCount;
static WCHAR Registered[1024], ServerExecutable[MAX_PATH];
static WCHAR ParsedExe[MAX_PATH], Service[64];
static PWSTR ParsedArgv[3];
static const CLSID *ExpectedClsid;
static LPUNKNOWN ExpectedOuter;
static ULONG ExpectedContext;
/* Fixed test fixtures are bounded by their declaring arrays. */
static PWSTR CopyString(PWSTR output, PCWSTR input)
{
    memcpy(output,input,(wcslen(input) + 1) * sizeof(WCHAR)); return output;
}
static PWSTR AppendString(PWSTR output, PCWSTR input)
{
    CopyString(output + wcslen(output),input); return output;
}
static int ObjectToken;
#define REAL_OBJECT ((PVOID)&ObjectToken)

static void Acquire(unsigned int index) { assert(Resources[index] == 0); ++Resources[index]; }
static void Release(unsigned int index) { assert(Resources[index] == 1); --Resources[index]; }
static void NoLeaks(void)
{
    unsigned int index;
    for (index = 0; index < ARRAYSIZE(Resources); ++index) assert(Resources[index] == 0);
    assert(!ArgvHeld);
}
static BOOLEAN StringEqual(PCWSTR first, PCWSTR second) { return (BOOLEAN)(wcscmp(first, second) == 0); }
static BOOLEAN StringEqualI(PCWSTR first, PCWSTR second)
{
    while (*first && *second) {
        if (towlower(*first++) != towlower(*second++)) return FALSE;
    }
    return (BOOLEAN)(*first == *second);
}
static BOOLEAN StringBeginsWith(PCWSTR text, PCWSTR prefix)
{
    return (BOOLEAN)(wcsncmp(text, prefix, wcslen(prefix)) == 0);
}
static HANDLE WINAPI MockGetCurrentThread(void) { return H(15); }
static HANDLE WINAPI MockGetCurrentProcess(void) { return H(14); }
static ULONG WINAPI MockGetLastError(void) { return LastError; }
static void WINAPI MockSetLastError(ULONG error) { LastError = error; }
static BOOL WINAPI MockCloseHandle(HANDLE handle)
{
    Release((unsigned int)(ULONG_PTR)handle);
    LastError = 700;
    return TRUE;
}
static BOOL WINAPI MockOpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token)
{
    assert(NativeCalls >= 1 && thread == H(15) && access == TOKEN_QUERY && as_self);
    LastError = ThreadTokenError ? ERROR_ACCESS_DENIED : ERROR_NO_TOKEN;
    if (!Impersonating) return FALSE;
    Acquire(1); *token = H(1); return TRUE;
}
static BOOL WINAPI MockOpenProcessToken(HANDLE process, DWORD access, PHANDLE token)
{
    assert(process == H(14) && access == TOKEN_QUERY);
    if (TokenOpenFail) return FALSE;
    Acquire(2); *token = H(2); return TRUE;
}
static BOOL WINAPI MockGetTokenInformation(HANDLE token, int kind, PVOID buffer, DWORD bytes, DWORD *size)
{
    assert(token == H(2) && Resources[2]);
    *size = bytes;
    if (TokenQueryFail) return FALSE;
    if (kind == TokenElevation) {
        ((TOKEN_ELEVATION *)buffer)->TokenIsElevated = TokenElevated;
    } else {
        assert(kind == TokenIntegrityLevel && bytes >= sizeof(TOKEN_MANDATORY_LABEL));
        ((TOKEN_MANDATORY_LABEL *)buffer)->Label.Sid = (PSID)&Integrity;
    }
    return TRUE;
}
static UCHAR * WINAPI MockGetSidSubAuthorityCount(PSID sid)
{
    static UCHAR count = 1;
    assert(sid == (PSID)&Integrity); return &count;
}
static ULONG * WINAPI MockGetSidSubAuthority(PSID sid, DWORD index)
{
    assert(sid == (PSID)&Integrity && index == 0); return &Integrity;
}
static LONG WINAPI MockRegOpenKeyExW(HKEY key, PCWSTR name, DWORD options, DWORD access, HKEY *result)
{
    ULONG status;
    assert(options == 0);
    if (key == HKEY_CURRENT_USER || key == HKEY_LOCAL_MACHINE) {
        assert(StringBeginsWith(name, L"Software\\Classes\\CLSID\\"));
        assert(access == (KEY_READ | KEY_WOW64_32KEY));
        status = key == HKEY_CURRENT_USER ? ClassOpenResult : MachineOpenResult;
        if (status) return (LONG)status;
        *result = key == HKEY_CURRENT_USER ? K(3) : K(4);
    } else if (StringEqual(name, L"TreatAs") || StringEqual(name, L"AutoTreatAs")) {
        if (!((TreatAs && StringEqual(name, L"TreatAs")) ||
              (AutoTreatAs && StringEqual(name, L"AutoTreatAs")))) return ERROR_FILE_NOT_FOUND;
        *result = K(5);
    } else {
        assert(key == K(3) && StringEqual(name, L"LocalServer32") && access == KEY_READ);
        if (LocalOpenResult) return (LONG)LocalOpenResult;
        *result = K(6);
    }
    Acquire((unsigned int)(ULONG_PTR)*result);
    LastError = 701; return ERROR_SUCCESS;
}
static LONG WINAPI MockRegCloseKey(HKEY key) { Release((unsigned int)(ULONG_PTR)key); return ERROR_SUCCESS; }
static LONG WINAPI MockRegQueryValueExW(HKEY key, PCWSTR name, DWORD *reserved, DWORD *type, PBYTE data, DWORD *bytes)
{
    PCWSTR value;
    DWORD required;
    assert(reserved == NULL);
    LastError = 702;
    if (key == K(3) || key == K(4)) {
        assert(name != NULL);
        if ((key == K(3) && ClassMetadata) || (key == K(4) && MachineMetadata)) {
            if ((ClassMetadata == 1 || MachineMetadata == 1) && StringEqual(name,L"AppID")) return ERROR_SUCCESS;
            if ((ClassMetadata == 2 || MachineMetadata == 2) && StringEqual(name,L"RunAs")) return ERROR_SUCCESS;
            if ((ClassMetadata == 3 || MachineMetadata == 3) && StringEqual(name,L"LocalService")) return ERROR_SUCCESS;
        }
        return ERROR_FILE_NOT_FOUND;
    }
    if (key == K(7)) {
        if (StringEqual(name, L"Debugger")) return Debugger ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
        assert(StringEqual(name, L"FilterFullPath"));
        value = BadFilter ? L"C:\\Other\\updater.exe" : ExpectedExe;
    } else {
        assert(key == K(6));
        if (name == NULL) value = Registered;
        else {
            assert(StringEqual(name, L"ServerExecutable"));
            if (!ServerExecutable[0]) return ERROR_FILE_NOT_FOUND;
            value = ServerExecutable;
        }
    }
    if (QueryFault) return (LONG)QueryFault;
    required = (DWORD)((wcslen(value) + 1) * sizeof(WCHAR));
    if (EmbeddedTail) required += sizeof(WCHAR);
    assert(type && data && bytes && *bytes >= required);
    *type = QueryType;
    memcpy(data, value, (wcslen(value) + 1) * sizeof(WCHAR));
    if (EmbeddedTail) ((PWSTR)data)[wcslen(value) + 1] = L'\0';
    if (BadTerminator) ((PWSTR)data)[required / sizeof(WCHAR) - 1] = L'X';
    *bytes = required;
    return ERROR_SUCCESS;
}
static BOOLEAN WINAPI MockKxCfgGetConfiguration(PCWSTR exe, KXCFG_PROGRAM_CONFIGURATION *config)
{
    assert(StringEqualI(exe,ExpectedExe)); *config = Config; return (BOOLEAN)!ConfigMissing;
}
static void WINAPI MockRtlInitUnicodeString(UNICODE_STRING *image, PCWSTR exe) { image->Buffer = exe; }
static NTSTATUS WINAPI MockLdrOpenImageFileOptionsKey(UNICODE_STRING *image, BOOL wow64, PHANDLE key)
{
    assert(StringEqualI(image->Buffer, ExpectedExe) && !wow64);
    if (OptionsOpenResult) return (NTSTATUS)0xc0000022U;
    Acquire(7); *key = H(7); return 0;
}
static LONG WINAPI MockInterlockedCompareExchange(volatile LONG *value, LONG exchange, LONG compare)
{
    LONG old;
    old = *value; if (old == compare) *value = exchange; return old;
}
static LONG WINAPI MockInterlockedExchange(volatile LONG *value, LONG exchange)
{
    LONG old;
    old = *value; *value = exchange; return old;
}
static HRESULT WINAPI MockSHGetFolderPathW(HANDLE window, int folder, HANDLE token, DWORD flags, PWSTR path)
{
    assert(!window && !token && folder == CSIDL_LOCAL_APPDATA && flags == SHGFP_TYPE_CURRENT);
    if (FailAppData) return E_FAIL;
    CopyString(path,L"C:\\Users\\Test\\AppData\\Local"); return S_OK;
}
static HRESULT MockStringCchPrintfW(PWSTR output, size_t capacity, PCWSTR format, ...)
{
    va_list args;
    PCWSTR value, prefix, suffix;
    size_t needed;
    if (FailPrintf) return E_FAIL;
    va_start(args, format); value = va_arg(args, PCWSTR); va_end(args);
    prefix = L""; suffix = L"";
    if (StringBeginsWith(format,L"Software")) prefix = L"Software\\Classes\\CLSID\\";
    else if (StringEqual(format,L"%s -Embedding")) suffix = L" -Embedding";
    else suffix = L"\\Google\\GoogleUpdater\\157.0.8089.0\\updater.exe";
    needed = wcslen(prefix) + wcslen(value) + wcslen(suffix) + 1;
    if (needed > capacity) return E_FAIL;
    CopyString(output,prefix); AppendString(output,value); AppendString(output,suffix); return S_OK;
}
static HRESULT MockStringCchCopyW(PWSTR output, size_t capacity, PCWSTR input)
{
    if (FailCopy || wcslen(input) + 1 > capacity) return E_FAIL;
    CopyString(output,input); return S_OK;
}
static ULONG WINAPI MockGetDriveTypeW(PCWSTR drive) { assert(StringEqual(drive,L"C:\\")); return TestDriveType; }
static PPWSTR WINAPI MockCommandLineToArgvW(PCWSTR command, INT *count)
{
    assert(StringEqual(command,Registered) && !ArgvHeld);
    if (!Argc) return NULL;
    *count = Argc; ArgvHeld = 1;
    ParsedArgv[0] = ParsedExe;
    ParsedArgv[1] = WrongArg ? L"--other" : L"--server";
    ParsedArgv[2] = Service; return ParsedArgv;
}
static PVOID WINAPI MockLocalFree(PVOID memory) { assert(memory == ParsedArgv && ArgvHeld); ArgvHeld = 0; return NULL; }
static ULONG WINAPI MockGetFileAttributesW(PCWSTR exe) { assert(StringEqualI(exe,ExpectedExe)); return Attributes; }
static HANDLE WINAPI MockCreateFileW(PCWSTR exe, DWORD access, DWORD sharing, PVOID security, DWORD disposition, DWORD flags, HANDLE template_file)
{
    assert(StringEqualI(exe,ExpectedExe) && access == GENERIC_READ && sharing == FILE_SHARE_READ);
    assert(!security && disposition == OPEN_EXISTING && flags == FILE_ATTRIBUTE_NORMAL && !template_file);
    if (ExeOpenFail) return INVALID_HANDLE_VALUE;
    Acquire(8); return H(8);
}
static DWORD WINAPI MockGetFinalPathNameByHandleW(HANDLE file, PWSTR output, DWORD capacity, DWORD flags)
{
    assert(file == H(8) && Resources[8] && capacity >= MAX_PATH && flags == 0);
    if (FinalPathZero) return 0;
    CopyString(output,BadCanonicalPath ? L"\\\\?\\C:\\Redirect\\updater.exe" : L"\\\\?\\");
    if (!BadCanonicalPath) AppendString(output,ExpectedExe);
    return FinalPathResult ? FinalPathResult : (DWORD)wcslen(output);
}
static HRESULT WINAPI MockCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, PPVOID object)
{
    HRESULT result;
    unsigned int index;
    assert(IsEqualGUID(clsid,ExpectedClsid) && outer == ExpectedOuter && context == ExpectedContext);
    assert(IsEqualGUID(iid,&RequestedIid));
    index = NativeCalls++; if (index >= NativeResultCount) index = NativeResultCount - 1;
    result = NativeResults[index];
    if (object) *object = SUCCEEDED(result) && !NullRealObject ? REAL_OBJECT : NULL;
    LastError = NativeCalls == 1 ? 611 : 744;
    return result;
}
static BOOL WINAPI MockCreateProcessW(PCWSTR exe, PWSTR command, PVOID process_security, PVOID thread_security, BOOL inherit, DWORD flags, PVOID environment, PCWSTR directory, STARTUPINFOW *startup, PROCESS_INFORMATION *process)
{
    WCHAR expected[1100], folder[MAX_PATH];
    PWSTR slash;
    assert(NativeCalls >= 2 && Resources[8] && ArgvHeld);
    assert(StringEqualI(exe,ExpectedExe));
    CopyString(expected,Registered); AppendString(expected,L" -Embedding"); assert(StringEqual(command,expected));
    CopyString(folder,ExpectedExe); slash = wcsrchr(folder,L'\\'); assert(slash); *slash = L'\0';
    assert(StringEqualI(directory,folder) && !process_security && !thread_security && !inherit && !flags && !environment);
    assert(startup->cb == sizeof(*startup));
    ++Spawns;
    if (LaunchFails) { LastError = LaunchError; return FALSE; }
    Acquire(9); Acquire(10); process->hProcess = H(9); process->hThread = H(10);
    process->dwProcessId = 4242; return TRUE;
}
static DWORD WINAPI MockGetTickCount(void) { return Clock; }
static DWORD WINAPI MockWaitForSingleObject(HANDLE process, DWORD milliseconds)
{
    assert(process == H(9) && !milliseconds); return ChildExited ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
static void WINAPI MockSleep(DWORD milliseconds) { assert(milliseconds == 100); Clock += milliseconds; ++Sleeps; }
static void MockLog(PCWSTR format, ...)
{
    va_list args;
    va_start(args,format); LoggedStage = va_arg(args,ULONG); va_end(args); LastError = 799;
}
static void MockLogEvent(int severity, PCWSTR format, ...) { (void)severity; (void)format; }
static void MockEnsureCOMInitialized(void) { ++Initialized; }
static HRESULT WINAPI MockGuidString(const CLSID *guid, LPOLESTR *text) { (void)guid; *text = NULL; return E_FAIL; }
static void WINAPI MockCoTaskMemFree(PVOID memory) { (void)memory; }

/* PRODUCTION_FUNCTIONS */

static void Reset(const CLSID *clsid)
{
    memset(&Data,0,sizeof(Data)); KexData = &Data;
    memset(&Config,0,sizeof(Config)); Config.Enabled = TRUE; Config.WinVerSpoof = WinVerSpoofWin10;
    SharedUserData = &Shared; Shared.NtMajorVersion = 6; Shared.NtMinorVersion = 1;
    LastError = 999; NativeCalls = Spawns = LoggedStage = Clock = Sleeps = 0;
    Integrity = 8192; TokenElevated = 0; TestDriveType = DRIVE_FIXED; Attributes = FILE_ATTRIBUTE_NORMAL;
    ClassOpenResult = LocalOpenResult = OptionsOpenResult = 0; MachineOpenResult = ERROR_FILE_NOT_FOUND;
    QueryType = REG_SZ; QueryFault = LaunchError = FinalPathResult = 0;
    Impersonating = ThreadTokenError = TokenOpenFail = TokenQueryFail = 0;
    ClassMetadata = MachineMetadata = TreatAs = AutoTreatAs = ConfigMissing = Debugger = 0;
    BadFilter = ExeOpenFail = BadCanonicalPath = EmbeddedTail = BadTerminator = 0;
    WrongArg = LaunchFails = ChildExited = NullRealObject = 0;
    FailAppData = FailPrintf = FailCopy = FinalPathZero = 0;
    Argc = 3; ArgvHeld = Initialized = 0;
    memset(Resources,0,sizeof(Resources));
    NativeResults[0] = BAD; NativeResults[1] = BAD; NativeResults[2] = S_OK; NativeResultCount = 3;
    CopyString(ParsedExe,ExpectedExe);
    CopyString(Service,IsEqualGUID(clsid,&Active) ? L"--service=update" : L"--service=update-internal");
    CopyString(Registered,L"\""); AppendString(Registered,ExpectedExe); AppendString(Registered,L"\" --server "); AppendString(Registered,Service);
    ServerExecutable[0] = L'\0';
    ExpectedClsid = clsid; ExpectedOuter = NULL; ExpectedContext = CLSCTX_LOCAL_SERVER;
    KxpUpdaterFallbackBusy = 0;
}
static HRESULT Invoke(PPVOID output)
{
    HRESULT result;
    result = Ext_CoCreateInstance(ExpectedClsid,ExpectedOuter,ExpectedContext,&RequestedIid,output);
    assert(Initialized == 1);
    NoLeaks(); ++Checks; return result;
}
static void Rejected(void)
{
    PVOID object;
    object = REAL_OBJECT;
    assert(Invoke(&object) == BAD && object == NULL);
    assert(Spawns == 0 && LastError == 611);
    assert(KxpUpdaterFallbackBusy == 0);
}
int main(void)
{
    PVOID object;
    CLSID unknown;
    unsigned int index;
    HRESULT other_results[3];
    other_results[0] = S_OK; other_results[1] = E_NOINTERFACE; other_results[2] = E_FAIL;

    /* Native-first: existing native success and unrelated failures never inspect
       configuration, start a process or replace the exact pointer/HRESULT. */
    for (index = 0; index < ARRAYSIZE(other_results); ++index) {
        Reset(&Internal); NativeResults[0] = other_results[index];
        assert(Invoke(&object) == other_results[index]);
        assert(NativeCalls == 1 && Spawns == 0 && LoggedStage == 0 && LastError == 611);
        assert(object == (SUCCEEDED(other_results[index]) ? REAL_OBJECT : NULL));
    }

    Reset(&Internal); unknown = Internal; ++unknown.Data1; ExpectedClsid = &unknown; Rejected();
    Reset(&Internal); ExpectedOuter = (LPUNKNOWN)REAL_OBJECT; Rejected();
    Reset(&Internal); ExpectedContext = CLSCTX_LOCAL_SERVER | 0x10; Rejected();
    Reset(&Internal); ExpectedContext = 0x1; Rejected();
    Reset(&Internal); assert(Invoke(NULL) == BAD && NativeCalls == 1 && !Spawns);
    Reset(&Internal); KexData = NULL; Rejected();
    Reset(&Internal); Data.IfeoParameters.DisableForChild = 1; Rejected();
    Reset(&Internal); Data.IfeoParameters.DisableAppSpecific = 1; Rejected();
    Reset(&Internal); Data.IfeoParameters.StrongVersionSpoof = 1; Rejected();
    Reset(&Internal); Shared.NtMajorVersion = 10; Rejected();
    Reset(&Internal); Shared.NtMinorVersion = 2; Rejected();
    Reset(&Internal); Integrity = 12288; Rejected();
    Reset(&Internal); Integrity = 4096; Rejected();
    Reset(&Internal); TokenElevated = 1; Rejected();
    Reset(&Internal); Impersonating = 1; Rejected();
    Reset(&Internal); ThreadTokenError = 1; Rejected();
    Reset(&Internal); TokenOpenFail = 1; Rejected();
    Reset(&Internal); TokenQueryFail = 1; Rejected();
    Reset(&Internal); FailAppData = 1; Rejected();
    Reset(&Internal); FailPrintf = 1; Rejected();
    Reset(&Internal); TestDriveType = 4; Rejected();
    Reset(&Internal); ClassOpenResult = ERROR_FILE_NOT_FOUND; Rejected();
    Reset(&Internal); MachineOpenResult = ERROR_ACCESS_DENIED; Rejected();
    Reset(&Internal); LocalOpenResult = ERROR_FILE_NOT_FOUND; Rejected();
    for (index = 1; index <= 3; ++index) {
        Reset(&Internal); ClassMetadata = (int)index; Rejected();
        Reset(&Internal); MachineOpenResult = 0; MachineMetadata = (int)index; Rejected();
    }
    Reset(&Internal); TreatAs = 1; Rejected();
    Reset(&Internal); AutoTreatAs = 1; Rejected();
    Reset(&Internal); QueryType = REG_EXPAND_SZ; Rejected();
    Reset(&Internal); QueryFault = ERROR_MORE_DATA; Rejected();
    Reset(&Internal); EmbeddedTail = 1; Rejected();
    Reset(&Internal); BadTerminator = 1; Rejected();
    Reset(&Internal); CopyString(ServerExecutable,L"C:\\Other\\updater.exe"); Rejected();
    Reset(&Internal); Argc = 0; Rejected();
    Reset(&Internal); Argc = 2; Rejected();
    Reset(&Internal); WrongArg = 1; Rejected();
    Reset(&Internal); CopyString(Service,L"--service=update"); Rejected();
    Reset(&Active); CopyString(Service,L"--service=update-internal"); Rejected();
    Reset(&Internal); CopyString(ParsedExe,L"C:\\Other\\updater.exe"); Rejected();
    Reset(&Internal); CopyString(ParsedExe,L"C:\\Users\\Test\\AppData\\Local\\Google\\GoogleUpdater\\157.0.8089.1\\updater.exe"); Rejected();
    Reset(&Internal); ConfigMissing = 1; Rejected();
    Reset(&Internal); Config.Enabled = 0; Rejected();
    Reset(&Internal); Config.DisableForChild = 1; Rejected();
    Reset(&Internal); Config.DisableAppSpecificHacks = 1; Rejected();
    Reset(&Internal); Config.WinVerSpoof = 3; Rejected();
    Reset(&Internal); Config.StrongSpoofOptions = 1; Rejected();
    Reset(&Internal); OptionsOpenResult = 1; Rejected();
    Reset(&Internal); BadFilter = 1; Rejected();
    Reset(&Internal); Debugger = 1; Rejected();
    Reset(&Internal); Attributes = INVALID_FILE_ATTRIBUTES; Rejected();
    Reset(&Internal); Attributes |= FILE_ATTRIBUTE_DIRECTORY; Rejected();
    Reset(&Internal); Attributes |= FILE_ATTRIBUTE_REPARSE_POINT; Rejected();
    Reset(&Internal); ExeOpenFail = 1; Rejected();
    Reset(&Internal); FinalPathZero = 1; Rejected();
    Reset(&Internal); FinalPathResult = MAX_PATH + 8; Rejected();
    Reset(&Internal); BadCanonicalPath = 1; Rejected();
    Reset(&Internal); FailCopy = 1; Rejected();

    /* Another native client may register the real object before spawning. */
    Reset(&Internal); NativeResults[1] = S_OK;
    assert(Invoke(&object) == S_OK && object == REAL_OBJECT && NativeCalls == 2 && !Spawns);
    Reset(&Internal); NativeResults[1] = E_NOINTERFACE; Rejected(); assert(NativeCalls == 2);

    /* Both exact registered services obtain a real native object after spawn. */
    for (index = 0; index < 2; ++index) {
        Reset(index ? &Active : &Internal);
        CopyString(ServerExecutable,ExpectedExe);
        assert(Invoke(&object) == S_OK && object == REAL_OBJECT);
        assert(Spawns == 1 && NativeCalls == 3 && LoggedStage == 8);
        assert(KxpUpdaterFallbackBusy == 0 && Sleeps == 0);
    }
    Reset(&Internal); MachineOpenResult = 0;
    assert(Invoke(&object) == S_OK && object == REAL_OBJECT && Spawns == 1);

    /* Launch failure/new COM errors are not converted to success or substituted
       for the caller's original HRESULT/LastError. Every handle is released. */
    Reset(&Internal); LaunchFails = 1; LaunchError = ERROR_ACCESS_DENIED;
    assert(Invoke(&object) == BAD && object == NULL && Spawns == 1 && NativeCalls == 2 && LastError == 611);
    Reset(&Internal); NativeResults[2] = E_NOINTERFACE;
    assert(Invoke(&object) == BAD && object == NULL && Spawns == 1 && NativeCalls == 3 && LastError == 611);
    Reset(&Internal); NativeResults[2] = BAD; ChildExited = 1;
    assert(Invoke(&object) == BAD && object == NULL && Spawns == 1 && !Sleeps && LastError == 611);
    Reset(&Internal); NativeResults[2] = BAD;
    assert(Invoke(&object) == BAD && object == NULL && Spawns == 1 && Sleeps == 50 && Clock == 5000 && LastError == 611);
    Reset(&Internal); NullRealObject = 1;
    assert(Invoke(&object) == BAD && object == NULL && LastError == 611);

    /* A concurrent/reentrant request must not clear another request's gate. */
    Reset(&Internal); KxpUpdaterFallbackBusy = 1;
    assert(Invoke(&object) == BAD && object == NULL && !Spawns && KxpUpdaterFallbackBusy == 1 && LastError == 611);
    KxpUpdaterFallbackBusy = 0;
    printf("updater COM: %lu real-function scenarios passed (all OS APIs doubled).\n",(unsigned long)Checks);
    return 0;
}
