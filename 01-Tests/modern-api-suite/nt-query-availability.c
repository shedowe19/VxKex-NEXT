#include <windows.h>
typedef LONG (NTAPI *QUERYPROCESS)(HANDLE,ULONG,PVOID,ULONG,PULONG);
LONG NTAPI TestQueryInformationProcess(HANDLE process,ULONG info,PVOID buffer,ULONG size,PULONG returned)
{
    union {FARPROC raw;QUERYPROCESS function;} native;
    if(info==58) return (LONG)0xc0000003;
    native.raw=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess");
    if(!native.raw)return (LONG)0xc0000002;
    return native.function(process,info,buffer,size,returned);
}
