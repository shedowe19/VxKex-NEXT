// Preserve a per-process NEXT profile across normal, same-user Windows UAC.
// Native ShellExecute remains authoritative for every unsupported request.
#ifdef KEX_UAC_TEST
#include "uac_test_shim.h"
#else
#include "buildcfg.h"
#include <KexUac.h>
#include <ShellApi.h>
#include <Sddl.h>
#include <WinCrypt.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif
#include <KexUacIo.h>

extern PKEX_PROCESS_DATA KexData;

static BOOLEAN KxuSamePath(PCWSTR Left, PCWSTR Right)
{
    return CompareStringOrdinal(Left, -1, Right, -1, TRUE) == CSTR_EQUAL;
}

static BOOLEAN KxuWithin(PCWSTR File, PCUNICODE_STRING Root)
{
    ULONG Cch;
    Cch = Root->Length / sizeof(WCHAR);
    return Cch && wcslen(File) >= Cch &&
        CompareStringOrdinal(File, Cch, Root->Buffer, Cch, TRUE) == CSTR_EQUAL &&
        (File[Cch] == 0 || File[Cch] == L'\\' || Root->Buffer[Cch-1] == L'\\');
}

static BOOLEAN KxuAbsoluteExe(PCWSTR File)
{
    ULONG Index, Cch;
    if (!File) return FALSE;
    Cch = (ULONG) wcslen(File);
    if (Cch < 7 || Cch >= MAX_PATH || File[1] != L':' || File[2] != L'\\' ||
        !((File[0] >= L'A' && File[0] <= L'Z') ||
          (File[0] >= L'a' && File[0] <= L'z')) ||
        !KxuSamePath(File+Cch-4, L".exe")) return FALSE;
    for (Index=3; Index<Cch; ++Index) {
        if (File[Index] == L':' || File[Index] == L'/' || File[Index] == L'"' ||
            File[Index] == L'*' || File[Index] == L'?') return FALSE;
    }
    return TRUE;
}

// An explicit target policy belongs to its owner. This bridge only supplies
// profiles for targets without an effective IFEO configuration of their own.
static BOOLEAN KxuUnconfigured(PCWSTR File)
{
    UNICODE_STRING Image;
    HANDLE Key;
    NTSTATUS Status;
    ULONG Index, Cch, Error;
    WCHAR Name[MAX_PATH];
    RtlInitUnicodeString(&Image, File);
    Key = NULL;
    Status = LdrOpenImageFileOptionsKey(&Image, FALSE, &Key);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
        return TRUE;
    if (!NT_SUCCESS(Status)) return FALSE;
    for (Index=0; ; ++Index) {
        Cch=ARRAYSIZE(Name);
        Error=RegEnumValueW((HKEY) Key, Index, Name, &Cch, NULL, NULL, NULL, NULL);
        if (Error==ERROR_NO_MORE_ITEMS) { NtClose(Key); return TRUE; }
        if (Error || (!KxuSamePath(Name, L"UseFilter") &&
            !KxuSamePath(Name, L"DisableExceptionChainValidation"))) break;
    }
    NtClose(Key);
    return FALSE;
}

static BOOLEAN KxuTokenSid(PWSTR *SidString)
{
    HANDLE Token, ThreadToken;
    TOKEN_ELEVATION_TYPE Elevation;
    PTOKEN_USER User;
    DWORD Size;
    BOOLEAN Success;
    *SidString=NULL;
    ThreadToken=NULL;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &ThreadToken)) {
        CloseHandle(ThreadToken); return FALSE;
    }
    if (GetLastError()!=ERROR_NO_TOKEN) return FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token)) return FALSE;
    Success=FALSE; User=NULL; Size=sizeof(Elevation);
    if (!GetTokenInformation(Token, TokenElevationType, &Elevation, Size, &Size) ||
        (Elevation!=TokenElevationTypeLimited && Elevation!=TokenElevationTypeFull)) goto Exit;
    Size=0;
    GetTokenInformation(Token, TokenUser, NULL, 0, &Size);
    if (!Size || Size>65536) goto Exit;
    User=HeapAlloc(GetProcessHeap(), 0, Size);
    if (!User || !GetTokenInformation(Token, TokenUser, User, Size, &Size)) goto Exit;
    Success=(BOOLEAN) ConvertSidToStringSidW(User->User.Sid, SidString);
Exit:
    if (User) HeapFree(GetProcessHeap(), 0, User);
    CloseHandle(Token);
    return Success;
}

static HANDLE KxuPinFile(PCWSTR File, BY_HANDLE_FILE_INFORMATION *Information)
{
    HANDLE Handle;
    DWORD Cch;
    WCHAR Actual[MAX_PATH+4], Expected[MAX_PATH+4];
    Handle=CreateFileW(File, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (Handle==INVALID_HANDLE_VALUE) return NULL;
    Cch=GetFinalPathNameByHandleW(Handle, Actual, ARRAYSIZE(Actual), FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if (!Cch || Cch>=ARRAYSIZE(Actual) ||
        FAILED(StringCchPrintf(Expected, ARRAYSIZE(Expected), L"\\\\?\\%s", File)) ||
        !KxuSamePath(Actual, Expected) || !GetFileInformationByHandle(Handle, Information) ||
        (Information->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(Handle); return NULL;
    }
    return Handle;
}

static BOOLEAN KxuProcessIdentity(HANDLE Process, ULONG Pid, ULONGLONG Time, PCWSTR File)
{
    FILETIME Created, Exited, Kernel, User;
    WCHAR Image[MAX_PATH];
    DWORD Size;
    Size=ARRAYSIZE(Image);
    return GetProcessId(Process)==Pid &&
        GetProcessTimes(Process, &Created, &Exited, &Kernel, &User) &&
        KxuFileTime(Created)==Time &&
        QueryFullProcessImageNameW(Process, 0, Image, &Size) && KxuSamePath(Image, File);
}

static HANDLE KxuCreatePipe(PCWSTR Nonce, PCWSTR Sid)
{
    WCHAR Name[128], Sddl[256];
    SECURITY_ATTRIBUTES Attributes;
    PSECURITY_DESCRIPTOR Descriptor;
    HANDLE Pipe;
    Descriptor=NULL;
    if (FAILED(StringCchPrintf(Name, ARRAYSIZE(Name), KXU_PIPE_PREFIX L"%s", Nonce)) ||
        FAILED(StringCchPrintf(Sddl, ARRAYSIZE(Sddl),
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;%s)", Sid)) ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl,
            SDDL_REVISION_1, &Descriptor, NULL)) return INVALID_HANDLE_VALUE;
    Attributes.nLength=sizeof(Attributes); Attributes.bInheritHandle=FALSE;
    Attributes.lpSecurityDescriptor=Descriptor;
    Pipe=CreateNamedPipeW(Name,
        PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
        1, sizeof(KXU_REQUEST), sizeof(KXU_REQUEST), KXU_TIMEOUT, &Attributes);
    LocalFree(Descriptor);
    return Pipe;
}

// FALSE means native fall-through is allowed and no consent was attempted.
// Once *Handled is set, no path may retry the original launch.
static BOOL KxuExecute(SHELLEXECUTEINFOW *Info, BOOLEAN *Handled)
{
    static CONST WCHAR Hex[]=L"0123456789abcdef";
    static CONST ULONG AllowedMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|
        SEE_MASK_FLAG_NO_UI|SEE_MASK_NOZONECHECKS|SEE_MASK_UNICODE;
    KXU_REQUEST *Request;
    KXU_MESSAGE Message, Ready;
    SHELLEXECUTEINFOW Native;
    BY_HANDLE_FILE_INFORMATION FileInfo, HelperInfo;
    FILETIME Created, Exited, Kernel, User;
    HANDLE TargetPin, HelperPin, Pipe, Target;
    HCRYPTPROV Crypto;
    WCHAR Helper[MAX_PATH], Nonce[KXU_NONCE_CCH], Arguments[160];
    BYTE Random[16];
    PWSTR Sid;
    DWORD Error, Index, PeerPid, CreationError;
    ULONG_PTR ShellError;
    ULONGLONG HelperTime;
    BOOL Success;
    *Handled=FALSE;
    Request=NULL; TargetPin=HelperPin=Target=NULL; Pipe=INVALID_HANDLE_VALUE;
    Crypto=0; Sid=NULL; ZeroMemory(&Native, sizeof(Native));
    if (!Info || Info->cbSize!=sizeof(*Info) ||
        (Info->fMask & ~AllowedMask) || !Info->lpVerb ||
        !KxuSamePath(Info->lpVerb,L"runas") || !KexData ||
        !KxuValidParameters(&KexData->IfeoParameters) ||
        !KxuAbsoluteExe(Info->lpFile) ||
        KxuWithin(Info->lpFile, &KexData->WinDir) ||
        KxuWithin(Info->lpFile, &KexData->KexDir) ||
        !KxuUnconfigured(Info->lpFile)) return FALSE;
    if (!KxuTokenSid(&Sid)) return FALSE;
    if (FAILED(StringCchPrintf(Helper, ARRAYSIZE(Helper), L"%.*s\\KexElevate.exe",
        KexData->Kex3264DirPath.Length / sizeof(WCHAR), KexData->Kex3264DirPath.Buffer))) goto Unsupported;
    TargetPin=KxuPinFile(Info->lpFile, &FileInfo);
    HelperPin=KxuPinFile(Helper, &HelperInfo);
    if (!TargetPin || !HelperPin) goto Unsupported;
    Request=HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Request));
    if (!Request) goto Unsupported;
    if (FAILED(StringCchCopy(Request->File, ARRAYSIZE(Request->File), Info->lpFile)) ||
        FAILED(StringCchCopy(Request->Arguments, ARRAYSIZE(Request->Arguments),
            Info->lpParameters ? Info->lpParameters : L""))) goto Unsupported;
    if (Info->lpDirectory && Info->lpDirectory[0]) {
        if (FAILED(StringCchCopy(Request->Directory, ARRAYSIZE(Request->Directory),
            Info->lpDirectory))) goto Unsupported;
    } else {
        Index=GetCurrentDirectoryW(ARRAYSIZE(Request->Directory), Request->Directory);
        if (!Index || Index>=ARRAYSIZE(Request->Directory)) goto Unsupported;
    }
    if (Request->Directory[1]!=L':' || Request->Directory[2]!=L'\\') goto Unsupported;
    Request->FileCch=(ULONG)wcslen(Request->File)+1;
    Request->DirectoryCch=(ULONG)wcslen(Request->Directory)+1;
    Request->ArgumentsCch=(ULONG)wcslen(Request->Arguments)+1;
    if (!KxuValidString(Request->File,Request->FileCch,ARRAYSIZE(Request->File)) ||
        !KxuValidString(Request->Arguments,Request->ArgumentsCch,ARRAYSIZE(Request->Arguments)) ||
        Request->FileCch+Request->ArgumentsCch+3>32767) goto Unsupported;
    Request->Parameters=KexData->IfeoParameters; Request->ShowWindow=Info->nShow;
    Request->CallerPid=GetCurrentProcessId();
    if (!GetProcessTimes(GetCurrentProcess(), &Created, &Exited, &Kernel, &User)) goto Unsupported;
    Request->CallerCreationTime=KxuFileTime(Created);
    Request->VolumeSerial=FileInfo.dwVolumeSerialNumber;
    Request->FileIndexHigh=FileInfo.nFileIndexHigh; Request->FileIndexLow=FileInfo.nFileIndexLow;
    Request->FileSizeHigh=FileInfo.nFileSizeHigh; Request->FileSizeLow=FileInfo.nFileSizeLow;
    Request->WriteTimeHigh=FileInfo.ftLastWriteTime.dwHighDateTime;
    Request->WriteTimeLow=FileInfo.ftLastWriteTime.dwLowDateTime;
    KxuHeader(&Request->Header,KxuRequest,sizeof(*Request));
    if (KxuValidateRequest(Request,Request->CallerPid,Request->CallerCreationTime)) goto Unsupported;
    if (!CryptAcquireContextW(&Crypto,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT) ||
        !CryptGenRandom(Crypto,sizeof(Random),Random)) goto Unsupported;
    for (Index=0;Index<sizeof(Random);++Index) {
        Nonce[2*Index]=Hex[Random[Index]>>4]; Nonce[2*Index+1]=Hex[Random[Index]&15];
    }
    Nonce[KXU_NONCE_CCH-1]=0;
    Pipe=KxuCreatePipe(Nonce,Sid);
    if (Pipe==INVALID_HANDLE_VALUE) goto Unsupported;
    if (FAILED(StringCchPrintf(Arguments,ARRAYSIZE(Arguments), L"--uac %s %lu %016I64x",
        Nonce,Request->CallerPid,Request->CallerCreationTime))) goto Unsupported;

    Native.cbSize=sizeof(Native);
    Native.fMask=Info->fMask|SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
    Native.hwnd=Info->hwnd; Native.lpVerb=L"runas"; Native.lpFile=Helper;
    Native.lpParameters=Arguments; Native.lpDirectory=Request->Directory;
    Native.nShow=SW_HIDE;
    *Handled=TRUE;
    Info->hProcess=NULL;
    Success=ShellExecuteExW(&Native);
    Error=GetLastError(); ShellError=(ULONG_PTR) Native.hInstApp;
    if (!Success) goto Finished;
    Error=ERROR_INVALID_HANDLE;
    if (!Native.hProcess) goto Finished;
    Error=ERROR_ACCESS_DENIED;
    if (!GetProcessTimes(Native.hProcess,&Created,&Exited,&Kernel,&User)) goto Finished;
    HelperTime=KxuFileTime(Created);
    Error=KxuConnect(Pipe,Native.hProcess);
    if (Error) goto Finished;
    Error=ERROR_ACCESS_DENIED;
    if (!GetNamedPipeClientProcessId(Pipe,&PeerPid) ||
        !KxuProcessIdentity(Native.hProcess,PeerPid,HelperTime,Helper)) goto Finished;
    Error=KxuRead(Pipe,&Message,sizeof(Message));
    if (Error) goto Finished;
    Error=ERROR_INVALID_DATA;
    if (!KxuValidHeader(&Message.Header,KxuHello,sizeof(Message)) || Message.Error ||
        Message.Pid!=PeerPid || Message.CreationTime!=HelperTime) goto Finished;
    Error=KxuWrite(Pipe,Request,sizeof(*Request));
    if (Error) goto Finished;
    Error=KxuRead(Pipe,&Ready,sizeof(Ready));
    if (Error) goto Finished;
    if (KxuValidHeader(&Ready.Header,KxuFailure,sizeof(Ready)) &&
        Ready.Pid==PeerPid && Ready.CreationTime==HelperTime && Ready.Error) {
        Error=Ready.Error; goto Finished;
    }
    Error=ERROR_INVALID_DATA;
    if (!KxuValidHeader(&Ready.Header,KxuChildReady,sizeof(Ready)) || Ready.Error ||
        !Ready.Pid || Ready.Pid==PeerPid) goto Finished;
    Target=OpenProcess(KXU_TARGET_ACCESS,FALSE,Ready.Pid);
    CreationError=GetLastError();
    if (!Target) { Error=CreationError; goto Finished; }
    Error=ERROR_ACCESS_DENIED;
    if (!KxuProcessIdentity(Target,Ready.Pid,Ready.CreationTime,Request->File)) goto Finished;
    Message=Ready;
    KxuHeader(&Message.Header,KxuAcknowledge,sizeof(Message));
    Error=KxuWrite(Pipe,&Message,sizeof(Message));
    if (Error) goto Finished;
    Error=KxuRead(Pipe,&Message,sizeof(Message));
    if (Error) goto Finished;
    Error=ERROR_INVALID_DATA;
    if (!KxuValidHeader(&Message.Header,KxuStarted,sizeof(Message)) || Message.Error ||
        Message.Pid!=Ready.Pid || Message.CreationTime!=Ready.CreationTime) goto Finished;
    Error=ERROR_SUCCESS;
    ShellError=33;
    if (Info->fMask&SEE_MASK_NOCLOSEPROCESS) { Info->hProcess=Target; Target=NULL; }
Finished:
    Success=Error==ERROR_SUCCESS;
    Info->hInstApp=(HINSTANCE)(Success ? 33 : (ShellError && ShellError<=32 ? ShellError : SE_ERR_ACCESSDENIED));
    if (Target) CloseHandle(Target);
    if (Native.hProcess) CloseHandle(Native.hProcess);
    if (Pipe!=INVALID_HANDLE_VALUE) CloseHandle(Pipe);
    if (Request) HeapFree(GetProcessHeap(),0,Request);
    if (TargetPin) CloseHandle(TargetPin);
    if (HelperPin) CloseHandle(HelperPin);
    if (Crypto) CryptReleaseContext(Crypto,0);
    if (Sid) LocalFree(Sid);
    SetLastError(Error);
    return Success;
Unsupported:
    if (Pipe!=INVALID_HANDLE_VALUE) CloseHandle(Pipe);
    if (Request) HeapFree(GetProcessHeap(),0,Request);
    if (TargetPin) CloseHandle(TargetPin);
    if (HelperPin) CloseHandle(HelperPin);
    if (Crypto) CryptReleaseContext(Crypto,0);
    if (Sid) LocalFree(Sid);
    return FALSE;
}

BOOL WINAPI KxuShellExecuteExW(SHELLEXECUTEINFOW *Info)
{
    BOOLEAN Handled;
    BOOL Success;
    DWORD SavedError;
    SavedError=GetLastError();
    Success=KxuExecute(Info,&Handled);
    if (Handled) return Success;
    SetLastError(SavedError);
    return ShellExecuteExW(Info);
}

static PWSTR KxuUnicode(PCSTR Text)
{
    int Cch;
    PWSTR Result;
    if (!Text) return NULL;
    Cch=MultiByteToWideChar(CP_ACP,0,Text,-1,NULL,0);
    if (!Cch) return NULL;
    Result=HeapAlloc(GetProcessHeap(),0,Cch*sizeof(WCHAR));
    if (Result && !MultiByteToWideChar(CP_ACP,0,Text,-1,Result,Cch)) {
        HeapFree(GetProcessHeap(),0,Result); return NULL;
    }
    return Result;
}

static BOOL KxuExecuteA(SHELLEXECUTEINFOA *Info, BOOLEAN *HandledOut)
{
    SHELLEXECUTEINFOW Wide;
    PWSTR Verb,File,Arguments,Directory;
    BOOLEAN Handled;
    BOOL Success;
    DWORD Error,SavedError;
    *HandledOut=FALSE;
    if (!Info || Info->cbSize!=sizeof(*Info)) return FALSE;
    SavedError=GetLastError();
    Verb=KxuUnicode(Info->lpVerb); File=KxuUnicode(Info->lpFile);
    Arguments=KxuUnicode(Info->lpParameters); Directory=KxuUnicode(Info->lpDirectory);
    Success=FALSE; Handled=FALSE;
    if ((!Info->lpVerb||Verb) && (!Info->lpFile||File) &&
        (!Info->lpParameters||Arguments) && (!Info->lpDirectory||Directory)) {
        ZeroMemory(&Wide,sizeof(Wide)); Wide.cbSize=sizeof(Wide);
        Wide.fMask=Info->fMask; Wide.hwnd=Info->hwnd; Wide.nShow=Info->nShow;
        Wide.lpVerb=Verb; Wide.lpFile=File; Wide.lpParameters=Arguments; Wide.lpDirectory=Directory;
        Success=KxuExecute(&Wide,&Handled);
        if (Handled) { Info->hProcess=Wide.hProcess; Info->hInstApp=Wide.hInstApp; }
    }
    Error=GetLastError();
    if (Verb) HeapFree(GetProcessHeap(),0,Verb);
    if (File) HeapFree(GetProcessHeap(),0,File);
    if (Arguments) HeapFree(GetProcessHeap(),0,Arguments);
    if (Directory) HeapFree(GetProcessHeap(),0,Directory);
    SetLastError(Handled?Error:SavedError);
    *HandledOut=Handled;
    return Success;
}

BOOL WINAPI KxuShellExecuteExA(SHELLEXECUTEINFOA *Info)
{
    BOOLEAN Handled;
    BOOL Success;
    Success=KxuExecuteA(Info,&Handled);
    return Handled?Success:ShellExecuteExA(Info);
}

HINSTANCE WINAPI KxuShellExecuteW(HWND Window,PCWSTR Verb,PCWSTR File,PCWSTR Arguments,PCWSTR Directory,INT Show)
{
    SHELLEXECUTEINFOW Info;
    BOOLEAN Handled;
    BOOL Success;
    DWORD Error;
    Error=GetLastError(); ZeroMemory(&Info,sizeof(Info)); Info.cbSize=sizeof(Info);
    Info.hwnd=Window; Info.lpVerb=Verb; Info.lpFile=File; Info.lpParameters=Arguments;
    Info.lpDirectory=Directory; Info.nShow=Show;
    Success=KxuExecute(&Info,&Handled);
    if (Handled) return Success?(HINSTANCE)(ULONG_PTR)33:Info.hInstApp;
    SetLastError(Error);
    return ShellExecuteW(Window,Verb,File,Arguments,Directory,Show);
}

HINSTANCE WINAPI KxuShellExecuteA(HWND Window,PCSTR Verb,PCSTR File,PCSTR Arguments,PCSTR Directory,INT Show)
{
    SHELLEXECUTEINFOA Info;
    BOOLEAN Handled;
    BOOL Success;
    DWORD Error;
    Error=GetLastError();
    ZeroMemory(&Info,sizeof(Info)); Info.cbSize=sizeof(Info);
    Info.hwnd=Window; Info.lpVerb=Verb; Info.lpFile=File; Info.lpParameters=Arguments;
    Info.lpDirectory=Directory; Info.nShow=Show;
    Success=KxuExecuteA(&Info,&Handled);
    if (Handled) return Success?(HINSTANCE)(ULONG_PTR)33:Info.hInstApp;
    SetLastError(Error);
    return ShellExecuteA(Window,Verb,File,Arguments,Directory,Show);
}
