#include "uac_test_shim.h"
/* ACTUAL_PROTOCOL */
/* ACTUAL_PARSERS */
static DWORD Fault,Reads,Writes,Resumes,Terminates,Creates,Closes,LastError,WriteTypes[8],OpenRights,PipeFlags;
static KXU_REQUEST Expected;
static KEX_PROCESS_DATA Data;
#define H(n) ((HANDLE)(ULONG_PTR)(n))
static DWORD WINAPI MockGetLastError(void) { return LastError; }
static PCWSTR WINAPI MockGetCommandLineW(void) { return L"fixture"; }
static PWSTR *WINAPI MockCommandLineToArgvW(PCWSTR Text,int *Count)
{
    PWSTR *Result;(void)Text;*Count=Fault==1?4:5;
    Result=(PWSTR*)calloc(5,sizeof(PWSTR));
    Result[0]=(PWSTR)L"C:\\Kex\\KexElevate.exe";Result[1]=(PWSTR)L"--uac";
    Result[2]=(PWSTR)L"0123456789abcdef0123456789abcdef";Result[3]=(PWSTR)L"101";Result[4]=(PWSTR)L"00000000000003e9";
    return Result;
}
static BOOL MockStringEqualW(PCWSTR Left,PCWSTR Right) { return !wcscmp(Left,Right); }
static HRESULT MockStringCchPrintfW(PWSTR Target,size_t Capacity,PCWSTR Format,...)
{ (void)Format;if(Capacity<2)return(HRESULT)-1;Target[0]=L'x';Target[1]=0;return 0; }
static HANDLE WINAPI MockCreateFileW(PCWSTR File,DWORD Access,DWORD Share,PVOID Security,DWORD Creation,DWORD Flags,HANDLE Template)
{ (void)File;(void)Access;(void)Share;(void)Security;(void)Creation;(void)Template;PipeFlags=Flags;return H(5); }
static BOOL WINAPI MockGetNamedPipeServerProcessId(HANDLE Pipe,ULONG *Pid)
{ (void)Pipe;*Pid=Fault==2?999:101;return TRUE; }
static HANDLE WINAPI MockOpenProcess(DWORD Access,BOOL Inherit,DWORD Pid)
{ OpenRights=Access;CHECK(!Inherit&&Pid==101);return H(1); }
static DWORD WINAPI MockWaitForSingleObject(HANDLE Handle,DWORD Timeout)
{ (void)Handle;CHECK(Timeout==0);return Fault==3?0:WAIT_TIMEOUT; }
static HANDLE WINAPI MockGetCurrentProcess(void) { return H(3); }
static DWORD WINAPI MockGetCurrentProcessId(void) { return 303; }
static HANDLE WINAPI MockGetProcessHeap(void) { return H(9); }
static PVOID WINAPI MockHeapAlloc(HANDLE Heap,DWORD Flags,size_t Size)
{ (void)Heap;(void)Flags;return calloc(1,Size); }
static BOOL WINAPI MockHeapFree(HANDLE Heap,DWORD Flags,PVOID Memory)
{ (void)Heap;(void)Flags;free(Memory);return TRUE; }
static PVOID WINAPI MockLocalFree(PVOID Memory) { free(Memory);return NULL; }
static BOOL WINAPI MockCloseHandle(HANDLE Handle) { (void)Handle;++Closes;return TRUE; }
static DWORD WINAPI MockRtlNtStatusToDosError(NTSTATUS Status) { (void)Status;return 193; }
static NTSTATUS WINAPI MockKexDataInitialize(PKEX_PROCESS_DATA *Output) { *Output=&Data;return 0; }
static NTSTATUS WINAPI MockKexInitializePropagation(void)
{ CHECK(!memcmp(&Data.IfeoParameters,&Expected.Parameters,sizeof(Expected.Parameters)));return Fault==7?(NTSTATUS)-1:0; }
static NTSTATUS WINAPI MockKexPatchCpiwSubsystemVersionCheck(void) { return Fault==8?(NTSTATUS)-1:0; }
static BOOL WINAPI MockCreateProcessW(PCWSTR File,PWSTR Command,PVOID ProcessSecurity,PVOID ThreadSecurity,BOOL Inherit,DWORD Flags,PVOID Environment,PCWSTR Directory,STARTUPINFOW *Startup,PROCESS_INFORMATION *Process)
{
    (void)Command;CHECK(!wcscmp(File,Expected.File)&&!wcscmp(Directory,Expected.Directory));
    CHECK(!ProcessSecurity&&!ThreadSecurity&&!Inherit&&!Environment&&Flags==CREATE_SUSPENDED);
    CHECK(Startup->dwFlags==STARTF_USESHOWWINDOW&&Startup->wShowWindow==Expected.ShowWindow);
    CHECK(!Resumes&&Writes==1);++Creates;if(Fault==9){LastError=193;return FALSE;}
    Process->hProcess=H(4);Process->hThread=H(7);Process->dwProcessId=404;return TRUE;
}
static BOOL WINAPI MockGetExitCodeProcess(HANDLE Process,DWORD *Exit)
{ CHECK(Process==H(4));*Exit=Fault==14?17:STILL_ACTIVE;return TRUE; }
static DWORD WINAPI MockResumeThread(HANDLE Thread)
{ CHECK(Thread==H(7)&&Reads==2&&Writes==2);++Resumes;if(Fault==15){LastError=5;return MAXDWORD;}if(Fault==16)return 0;if(Fault==17)return 2;return 1; }
static BOOL WINAPI MockTerminateProcess(HANDLE Process,UINT Exit)
{ CHECK(Process==H(4)&&Exit!=0);++Terminates;return TRUE; }
static DWORD KxuProcessTime(HANDLE Process,PULONGLONG Time)
{ *Time=Process==H(1)?1001:(Process==H(3)?3003:4004);return Fault==4?5:0; }
static DWORD KxuValidateCaller(HANDLE Caller) { CHECK(Caller==H(1));return Fault==5?5:0; }
static DWORD KxuPinImage(const KXU_REQUEST *Request,HANDLE *Image)
{ CHECK(!memcmp(Request,&Expected,sizeof(*Request)));*Image=H(6);return Fault==6?5:0; }
static DWORD KxuWrite(HANDLE Pipe,const VOID *Buffer,DWORD Size)
{
    const KXU_MESSAGE *Message;(void)Pipe;CHECK(Size==sizeof(*Message));Message=(const KXU_MESSAGE*)Buffer;
    WriteTypes[Writes++]=Message->Header.Type;
    if(Message->Header.Type==KxuChildReady)CHECK(Message->Pid==404&&Message->CreationTime==4004&&!Resumes);
    if(Message->Header.Type==KxuStarted)CHECK(Resumes==1&&!Terminates&&Message->Pid==404);
    return Fault==18&&Message->Header.Type==KxuStarted?ERROR_TIMEOUT:0;
}
static DWORD KxuRead(HANDLE Pipe,PVOID Buffer,DWORD Size)
{
    KXU_MESSAGE *Message;(void)Pipe;++Reads;
    if(Reads==1){CHECK(Size==sizeof(Expected));memcpy(Buffer,&Expected,Size);if(Fault==10)((KXU_REQUEST*)Buffer)->CallerPid++;return 0;}
    CHECK(Size==sizeof(KXU_MESSAGE)&&!Resumes);Message=(KXU_MESSAGE*)Buffer;ZeroMemory(Message,sizeof(*Message));
    KxuHeader(&Message->Header,Fault==11?KxuAbort:KxuAcknowledge,sizeof(*Message));Message->Pid=404;Message->CreationTime=4004;
    if(Fault==12)Message->Pid++;
    if(Fault==13)return ERROR_TIMEOUT;
    return 0;
}
#define GetLastError MockGetLastError
#define GetCommandLineW MockGetCommandLineW
#define CommandLineToArgvW MockCommandLineToArgvW
#define StringEqualW MockStringEqualW
#define StringCchPrintfW MockStringCchPrintfW
#define CreateFileW MockCreateFileW
#define GetNamedPipeServerProcessId MockGetNamedPipeServerProcessId
#define OpenProcess MockOpenProcess
#define WaitForSingleObject MockWaitForSingleObject
#define GetCurrentProcess MockGetCurrentProcess
#define GetCurrentProcessId MockGetCurrentProcessId
#define GetProcessHeap MockGetProcessHeap
#define HeapAlloc MockHeapAlloc
#define HeapFree MockHeapFree
#define LocalFree MockLocalFree
#define CloseHandle MockCloseHandle
#define RtlNtStatusToDosError MockRtlNtStatusToDosError
#define KexDataInitialize MockKexDataInitialize
#define KexInitializePropagation MockKexInitializePropagation
#define KexPatchCpiwSubsystemVersionCheck MockKexPatchCpiwSubsystemVersionCheck
#define CreateProcessW MockCreateProcessW
#define GetExitCodeProcess MockGetExitCodeProcess
#define ResumeThread MockResumeThread
#define TerminateProcess MockTerminateProcess
/* ACTUAL_FUNCTIONS */
static void Reset(void)
{
    ZeroMemory(&Expected,sizeof(Expected));ZeroMemory(&Data,sizeof(Data));ZeroMemory(WriteTypes,sizeof(WriteTypes));
    KxuHeader(&Expected.Header,KxuRequest,sizeof(Expected));Expected.CallerPid=101;Expected.CallerCreationTime=1001;
    memcpy(Expected.File,L"C:\\Vendor\\app.exe",sizeof(L"C:\\Vendor\\app.exe"));memcpy(Expected.Directory,L"C:\\Work",sizeof(L"C:\\Work"));
    Expected.FileCch=(ULONG)wcslen(Expected.File)+1;Expected.DirectoryCch=(ULONG)wcslen(Expected.Directory)+1;Expected.ArgumentsCch=1;
    Expected.Parameters.DisableAppSpecific=1;Expected.Parameters.WinVerSpoof=WinVerSpoofWin10;Expected.Parameters.StrongVersionSpoof=3;
    Expected.Parameters.DisableConsoleEnhancements=1;Expected.Parameters.TlsForceEnabledProtocols=0x800;Expected.Parameters.TlsForceDisabledProtocols=0x80;
    Fault=Reads=Writes=Resumes=Terminates=Creates=Closes=LastError=OpenRights=PipeFlags=0;
}
int main(void)
{
    DWORD Index,Result;FILETIME Time;
    Reset();CHECK(KxuElevateMain()==0);CHECK(Resumes==1&&Creates==1&&!Terminates&&OpenRights==0x101000);
    CHECK((PipeFlags&(SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION))==(SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION));
    CHECK(Writes==3&&WriteTypes[0]==KxuHello&&WriteTypes[1]==KxuChildReady&&WriteTypes[2]==KxuStarted);
    for(Index=1;Index<=18;++Index){Reset();Fault=Index;Result=KxuElevateMain();CHECK(Result!=0);if(Index<=10)CHECK(!Resumes);if(Index>=11&&Index<=15)CHECK(Terminates==1);if(Index==16)CHECK(Resumes==1&&!Terminates);if(Index==17)CHECK(Resumes==1&&Terminates==1);if(Index==18)CHECK(Resumes==1&&!Terminates);}
    Reset();Expected.Parameters.DisableForChild=1;CHECK(KxuElevateMain()==ERROR_INVALID_PARAMETER&&!Creates&&!Resumes);
    /* Exercise every static shared helper on SDK /W4 builds as well. */
    Time.dwHighDateTime=0;Time.dwLowDateTime=1;CHECK(KxuFileTime(Time)==1);
    printf("UAC actual helper ACK/resume/cleanup: %d checks passed.\n",Checks);return 0;
}
