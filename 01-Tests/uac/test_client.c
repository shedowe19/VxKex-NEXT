#include "uac_test_shim.h"
/* ACTUAL_PROTOCOL */
static KEX_PROCESS_DATA Data;
static PKEX_PROCESS_DATA KexData=&Data;
static DWORD LastError,Fault,Reads,Writes,Prompts,NativeFallback,OpenRights,ClosedTarget,ClosedHelper;
static DWORD EntryError,NativeError,NativeReturn,InputMask,InputShow;
static KXU_REQUEST Captured;
static int Eligible,Configured,PinAllowed,HeapAllowed;
#define H(n) ((HANDLE)(ULONG_PTR)(n))
static DWORD WINAPI MockGetLastError(void) { return LastError; }
static VOID WINAPI MockSetLastError(DWORD Error) { LastError=Error; }
static HANDLE WINAPI MockGetProcessHeap(void) { return H(9); }
static PVOID WINAPI MockHeapAlloc(HANDLE Heap,DWORD Flags,size_t Size)
{ (void)Heap; (void)Flags; return HeapAllowed?calloc(1,Size):NULL; }
static BOOL WINAPI MockHeapFree(HANDLE Heap,DWORD Flags,PVOID Memory)
{ (void)Heap; (void)Flags; free(Memory); LastError=777; return TRUE; }
static PVOID WINAPI MockLocalFree(PVOID Memory) { free(Memory); LastError=777; return NULL; }
static BOOL WINAPI MockCloseHandle(HANDLE Handle)
{ if(Handle==H(4)) ++ClosedTarget; if(Handle==H(3)) ++ClosedHelper; LastError=777; return TRUE; }
static DWORD WINAPI MockGetCurrentProcessId(void) { return 101; }
static HANDLE WINAPI MockGetCurrentProcess(void) { return H(1); }
static BOOL WINAPI MockGetProcessTimes(HANDLE Process,FILETIME *Created,FILETIME *Exited,FILETIME *Kernel,FILETIME *User)
{ (void)Exited; (void)Kernel; (void)User; if(Fault==3&&Process==H(3)){LastError=5;return FALSE;} Created->dwHighDateTime=0;Created->dwLowDateTime=Process==H(1)?1001:3003;return TRUE; }
static DWORD WINAPI MockGetCurrentDirectoryW(DWORD Capacity,PWSTR Text)
{ (void)Capacity; memcpy(Text,L"C:\\Work",sizeof(L"C:\\Work")); return 7; }
static BOOL WINAPI MockCryptAcquireContextW(HCRYPTPROV *Crypto,PCWSTR Container,PCWSTR Provider,DWORD Type,DWORD Flags)
{ (void)Container;(void)Provider;(void)Type;(void)Flags;*Crypto=1;return TRUE; }
static BOOL WINAPI MockCryptGenRandom(HCRYPTPROV Crypto,DWORD Size,BYTE *Bytes)
{ (void)Crypto; memset(Bytes,0xab,Size);return TRUE; }
static BOOL WINAPI MockCryptReleaseContext(HCRYPTPROV Crypto,DWORD Flags)
{ (void)Crypto;(void)Flags;LastError=777;return TRUE; }
static BOOL WINAPI MockGetNamedPipeClientProcessId(HANDLE Pipe,ULONG *Pid)
{ (void)Pipe;*Pid=Fault==5?999:303;return TRUE; }
static HANDLE WINAPI MockOpenProcess(DWORD Access,BOOL Inherit,DWORD Pid)
{ OpenRights=Access;CHECK(!Inherit&&Pid==404);if(Fault==9){LastError=5;return NULL;}return H(4); }
static HRESULT MockStringCchCopy(PWSTR Target,size_t Capacity,PCWSTR Source)
{ size_t Size;Size=wcslen(Source)+1;if(Size>Capacity)return (HRESULT)-1;memcpy(Target,Source,Size*sizeof(WCHAR));return 0; }
/* String formatting is doubled, not used to validate the path/nonce protocol. */
static HRESULT MockStringCchPrintf(PWSTR Target,size_t Capacity,PCWSTR Format,...)
{ (void)Format;return MockStringCchCopy(Target,Capacity,L"C:\\Kex\\KexElevate.exe"); }
static int WINAPI MockMultiByteToWideChar(unsigned int Page,DWORD Flags,PCSTR Text,int Bytes,PWSTR Wide,int Capacity)
{ int Cch,Index;(void)Page;(void)Flags;(void)Bytes;Cch=(int)strlen(Text)+1;if(Wide){if(Cch>Capacity)return 0;for(Index=0;Index<Cch;++Index)Wide[Index]=(WCHAR)(unsigned char)Text[Index];}return Cch; }
static BOOL WINAPI MockShellExecuteExW(SHELLEXECUTEINFOW *Info)
{
    if(wcscmp(Info->lpFile,L"C:\\Kex\\KexElevate.exe")){++NativeFallback;EntryError=LastError;LastError=NativeError;return (BOOL)NativeReturn;}
    ++Prompts;InputMask=Info->fMask;InputShow=(DWORD)Info->nShow;
    if(Fault==1){LastError=ERROR_CANCELLED;Info->hInstApp=(HINSTANCE)(ULONG_PTR)SE_ERR_ACCESSDENIED;return FALSE;}
    Info->hProcess=Fault==2?NULL:H(3);Info->hInstApp=(HINSTANCE)(ULONG_PTR)33;return TRUE;
}
static BOOL WINAPI MockShellExecuteExA(SHELLEXECUTEINFOA *Info)
{ (void)Info;NativeFallback+=10;EntryError=LastError;LastError=NativeError;return (BOOL)NativeReturn; }
static HINSTANCE WINAPI MockShellExecuteW(HWND Window,PCWSTR Verb,PCWSTR File,PCWSTR Arguments,PCWSTR Directory,INT Show)
{ (void)Window;(void)Verb;(void)File;(void)Arguments;(void)Directory;(void)Show;NativeFallback+=100;EntryError=LastError;LastError=NativeError;return(HINSTANCE)(ULONG_PTR)37; }
static HINSTANCE WINAPI MockShellExecuteA(HWND Window,PCSTR Verb,PCSTR File,PCSTR Arguments,PCSTR Directory,INT Show)
{ (void)Window;(void)Verb;(void)File;(void)Arguments;(void)Directory;(void)Show;NativeFallback+=1000;EntryError=LastError;LastError=NativeError;return(HINSTANCE)(ULONG_PTR)38; }
static BOOLEAN KxuSamePath(PCWSTR Left,PCWSTR Right) { return !wcscmp(Left,Right); }
static BOOLEAN KxuWithin(PCWSTR File,const TEST_UNICODE_STRING *Root) { (void)File;(void)Root;return FALSE; }
static BOOLEAN KxuAbsoluteExe(PCWSTR File) { return File&&File[0]==L'C'; }
static BOOLEAN KxuUnconfigured(PCWSTR File) { (void)File;return (BOOLEAN)!Configured; }
static BOOLEAN KxuTokenSid(PWSTR *Sid)
{ if(!Eligible)return FALSE;*Sid=(PWSTR)calloc(32,sizeof(WCHAR));return *Sid!=NULL; }
static HANDLE KxuPinFile(PCWSTR File,BY_HANDLE_FILE_INFORMATION *Info)
{ (void)File;ZeroMemory(Info,sizeof(*Info));Info->dwVolumeSerialNumber=88;return PinAllowed?H(2):NULL; }
static HANDLE KxuCreatePipe(PCWSTR Nonce,PCWSTR Sid)
{ (void)Sid;CHECK(wcslen(Nonce)==32);return H(5); }
static BOOLEAN KxuProcessIdentity(HANDLE Process,ULONG Pid,ULONGLONG Time,PCWSTR File)
{ (void)File;if(Process==H(3))return Pid==303&&Time==3003&&Fault!=6;return Process==H(4)&&Pid==404&&Time==4004&&Fault!=10; }
static DWORD KxuConnect(HANDLE Pipe,HANDLE Helper) { (void)Pipe;(void)Helper;return Fault==4?ERROR_TIMEOUT:0; }
static DWORD KxuWrite(HANDLE Pipe,const VOID *DataPointer,DWORD Size)
{
    const KXU_MESSAGE *Message;(void)Pipe;++Writes;
    if(Size==sizeof(KXU_REQUEST)){memcpy(&Captured,DataPointer,Size);return Fault==7?ERROR_TIMEOUT:0;}
    Message=(const KXU_MESSAGE*)DataPointer;CHECK(KxuValidHeader(&Message->Header,KxuAcknowledge,sizeof(*Message)));
    CHECK(Message->Pid==404&&Message->CreationTime==4004&&!Message->Error);return Fault==11?ERROR_TIMEOUT:0;
}
static DWORD KxuRead(HANDLE Pipe,PVOID Buffer,DWORD Size)
{
    KXU_MESSAGE *Message;(void)Pipe;CHECK(Size==sizeof(KXU_MESSAGE));Message=(KXU_MESSAGE*)Buffer;
    ++Reads;ZeroMemory(Message,sizeof(*Message));
    if(Reads==1){KxuHeader(&Message->Header,KxuHello,sizeof(*Message));Message->Pid=303;Message->CreationTime=3003;if(Fault==8)Message->Pid=999;}
    else if(Reads==2){KxuHeader(&Message->Header,Fault==14?KxuFailure:KxuChildReady,sizeof(*Message));Message->Pid=Fault==14?303:404;Message->CreationTime=Fault==14?3003:4004;if(Fault==14)Message->Error=193;}
    else {KxuHeader(&Message->Header,KxuStarted,sizeof(*Message));Message->Pid=404;Message->CreationTime=4004;if(Fault==12)return ERROR_TIMEOUT;if(Fault==13)Message->CreationTime++;}
    return 0;
}
#define GetLastError MockGetLastError
#define SetLastError MockSetLastError
#define GetProcessHeap MockGetProcessHeap
#define HeapAlloc MockHeapAlloc
#define HeapFree MockHeapFree
#define LocalFree MockLocalFree
#define CloseHandle MockCloseHandle
#define GetCurrentProcessId MockGetCurrentProcessId
#define GetCurrentProcess MockGetCurrentProcess
#define GetProcessTimes MockGetProcessTimes
#define GetCurrentDirectoryW MockGetCurrentDirectoryW
#define CryptAcquireContextW MockCryptAcquireContextW
#define CryptGenRandom MockCryptGenRandom
#define CryptReleaseContext MockCryptReleaseContext
#define GetNamedPipeClientProcessId MockGetNamedPipeClientProcessId
#define OpenProcess MockOpenProcess
#define StringCchCopy MockStringCchCopy
#define StringCchPrintf MockStringCchPrintf
#define MultiByteToWideChar MockMultiByteToWideChar
#define ShellExecuteExW MockShellExecuteExW
#define ShellExecuteExA MockShellExecuteExA
#define ShellExecuteW MockShellExecuteW
#define ShellExecuteA MockShellExecuteA
/* ACTUAL_FUNCTIONS */
static void Reset(SHELLEXECUTEINFOW *Info)
{
    ZeroMemory(&Data,sizeof(Data));ZeroMemory(Info,sizeof(*Info));ZeroMemory(&Captured,sizeof(Captured));
    Data.IfeoParameters.DisableAppSpecific=1;Data.IfeoParameters.WinVerSpoof=WinVerSpoofWin10;
    Data.IfeoParameters.StrongVersionSpoof=3;Data.IfeoParameters.DisableConsoleEnhancements=1;
    Data.IfeoParameters.TlsForceEnabledProtocols=0x800;Data.IfeoParameters.TlsForceDisabledProtocols=0x80;
    KexData=&Data;Eligible=PinAllowed=HeapAllowed=1;Configured=0;Fault=Reads=Writes=Prompts=NativeFallback=0;
    OpenRights=ClosedTarget=ClosedHelper=InputMask=InputShow=0;LastError=12345;NativeReturn=TRUE;NativeError=54321;
    Info->cbSize=sizeof(*Info);Info->fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI|SEE_MASK_NOZONECHECKS;
    Info->lpVerb=L"runas";Info->lpFile=L"C:\\Vendor\\app.exe";Info->lpDirectory=L"C:\\Work";Info->lpParameters=L"--original argument";Info->nShow=1;
}
int main(void)
{
    SHELLEXECUTEINFOW Info;SHELLEXECUTEINFOA Ansi;DWORD Index;BOOLEAN Handled;BOOL Result;
    Reset(&Info);CHECK(KxuShellExecuteExW(&Info));CHECK(Prompts==1&&!NativeFallback);
    CHECK(Info.hProcess==H(4)&&ClosedTarget==0&&ClosedHelper==1&&OpenRights==0x101000);
    CHECK(InputMask==Info.fMask&&InputShow==SW_HIDE&&Reads==3&&Writes==2&&LastError==0);
    CHECK(!memcmp(&Captured.Parameters,&Data.IfeoParameters,sizeof(Data.IfeoParameters)));
    CHECK(Captured.ShowWindow==1&&!wcscmp(Captured.Arguments,Info.lpParameters)&&!wcscmp(Captured.Directory,Info.lpDirectory));
    Reset(&Info);Info.fMask=0;CHECK(KxuShellExecuteExW(&Info));CHECK(!Info.hProcess&&ClosedTarget==1);
    CHECK((InputMask&(SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC))==(SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC));
    for(Index=1;Index<=14;++Index){Reset(&Info);Fault=Index;CHECK(!KxuShellExecuteExW(&Info));CHECK(Prompts==1&&!NativeFallback&&!Info.hProcess);CHECK(LastError!=0&&LastError!=777);if(Index>=10&&Index<=13)CHECK(ClosedTarget==1);if(Index<=10)CHECK(Writes<=1);}
    Reset(&Info);Fault=1;CHECK(!KxuShellExecuteExW(&Info)&&LastError==ERROR_CANCELLED&&Reads==0);
    Reset(&Info);Fault=14;CHECK(!KxuShellExecuteExW(&Info)&&LastError==193&&Writes==1);
    Reset(&Info);Info.lpVerb=L"open";CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1&&EntryError==12345&&LastError==54321);
    Reset(&Info);Info.fMask|=0x80000000U;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);Eligible=0;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);Configured=1;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);PinAllowed=0;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1&&EntryError==12345);
    Reset(&Info);HeapAllowed=0;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);Info.nShow=12;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);Info.lpDirectory=L"C:\\percent%";CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);Data.IfeoParameters.TlsForceDisabledProtocols=0x800;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);KexData=NULL;CHECK(KxuShellExecuteExW(&Info)&&!Prompts&&NativeFallback==1);
    Reset(&Info);Info.lpDirectory=NULL;Result=KxuExecute(&Info,&Handled);CHECK(Result&&Handled&&!wcscmp(Captured.Directory,L"C:\\Work"));
    Reset(&Info);ZeroMemory(&Ansi,sizeof(Ansi));Ansi.cbSize=sizeof(Ansi);Ansi.lpVerb="runas";Ansi.lpFile="C:\\Vendor\\app.exe";Ansi.nShow=1;Ansi.fMask=SEE_MASK_NOCLOSEPROCESS;
    CHECK(KxuShellExecuteExA(&Ansi)&&Ansi.hProcess==H(4)&&Prompts==1&&!NativeFallback);
    Reset(&Info);Ansi.lpVerb="open";CHECK(KxuShellExecuteExA(&Ansi)&&NativeFallback==10&&!Prompts&&EntryError==12345);
    Reset(&Info);CHECK(KxuShellExecuteW(NULL,L"open",L"C:\\app.exe",NULL,NULL,1)==(HINSTANCE)(ULONG_PTR)37);CHECK(NativeFallback==100&&EntryError==12345);
    Reset(&Info);CHECK(KxuShellExecuteA(NULL,"open","C:\\app.exe",NULL,NULL,1)==(HINSTANCE)(ULONG_PTR)38);CHECK(NativeFallback==1000&&EntryError==12345);
    printf("UAC actual client handshake/state: %d checks passed.\n",Checks);return 0;
}
