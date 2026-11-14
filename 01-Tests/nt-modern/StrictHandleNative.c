/* Native self-process diagnostic. Each invocation is a disposable test child.
 * No hooks, synthetic exceptions, foreign process, registry, PEB or OS flags writes. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (NTAPI *QUERY_PROCESS)(HANDLE,ULONG,PVOID,ULONG,PULONG);
typedef LONG (NTAPI *SET_PROCESS)(HANDLE,ULONG,PVOID,ULONG);
typedef LONG (NTAPI *CLOSE_NATIVE)(HANDLE);
typedef LONG (NTAPI *QUERY_OBJECT)(HANDLE,ULONG,PVOID,ULONG,PULONG);
typedef ULONG (NTAPI *GET_FLAGS)(void);
typedef BOOL (WINAPI *GET_MITIGATION)(HANDLE,ULONG,PVOID,SIZE_T);
typedef BOOL (WINAPI *SET_MITIGATION)(ULONG,PVOID,SIZE_T);
typedef union FN { FARPROC Raw; LPCWSTR Address; QUERY_PROCESS Query; SET_PROCESS Set;
 CLOSE_NATIVE Close; QUERY_OBJECT Object; GET_FLAGS Flags; GET_MITIGATION GetPolicy; SET_MITIGATION SetPolicy; } FN;
static FN Query,Set,CloseNt,Object,Flags,GetPolicy,SetPolicy;

static BOOL OwnedBy(HMODULE Module,FN Value)
{
 HMODULE Owner=NULL;
 return Value.Raw && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,Value.Address,&Owner) && Owner==Module;
}
static void QueryState(const char *Stage,ULONG Class)
{
 union { ULONGLONG Align; BYTE Bytes[1024]; } Data;
 ULONG Length=0xc0dec0de;
 LONG Status; DWORD Exception=0;
 memset(&Data,0xa5,sizeof(Data));
 if(Class==52) {((ULONG*)Data.Bytes)[0]=3;((ULONG*)Data.Bytes)[1]=0xa5a5a5a5;}
 __try { Status=Query.Query(GetCurrentProcess(),Class,Data.Bytes,Class==52?8:(Class==54?4:1024),&Length); }
 __except(EXCEPTION_EXECUTE_HANDLER) {Status=(LONG)0xc0000001;Exception=GetExceptionCode();}
 printf("STATE|%s|class=%lu|status=0x%08lx|length=0x%08lx|exception=0x%08lx",
  Stage,Class,(ULONG)Status,Length,Exception);
 if(Class==52 && Status>=0) printf("|policy_field=%lu|raw_flags=0x%08lx",((ULONG*)Data.Bytes)[0],((ULONG*)Data.Bytes)[1]);
 if(Class==54 && Status>=0) printf("|raw_mode=0x%08lx",((ULONG*)Data.Bytes)[0]);
 printf("\n");
 if(Class==52 && GetPolicy.Raw) {
  ULONG PolicyFlags=0xa5a5a5a5;BOOL Ok;DWORD Error;
  SetLastError(0x6b145239);Ok=GetPolicy.GetPolicy(GetCurrentProcess(),3,&PolicyFlags,sizeof(PolicyFlags));Error=GetLastError();
  printf("PUBLIC_GET|%s|ok=%u|flags=0x%08lx|last_error=0x%08lx\n",Stage,Ok,PolicyFlags,Error);
 }
}
static LONG SetState(const char *Stage,ULONG Class,ULONG Value,BOOL DisableTrace)
{
 ULONG Data[2];LONG Status;DWORD Exception=0;
 Data[0]=Class==52?3:Value;Data[1]=Value;
 __try {Status=Set.Set(GetCurrentProcess(),Class,DisableTrace?NULL:Data,DisableTrace?0:(Class==52?8:4));}
 __except(EXCEPTION_EXECUTE_HANDLER) {Status=(LONG)0xc0000001;Exception=GetExceptionCode();}
 printf("SET|%s|class=%lu|value=0x%08lx|null_disable=%u|status=0x%08lx|exception=0x%08lx\n",
  Stage,Class,Value,DisableTrace,(ULONG)Status,Exception);
 return Status;
}
static void InvalidClose(const char *Stage)
{
 HANDLE Invalid=(HANDLE)(ULONG_PTR)0x41414140;
 BYTE Data[128];ULONG Length=0;LONG Preflight,Status=(LONG)0x7a716263;DWORD Exception=0;
 __try {Preflight=Object.Object(Invalid,0,Data,sizeof(Data),&Length);}
 __except(EXCEPTION_EXECUTE_HANDLER) {Preflight=(LONG)GetExceptionCode();}
 if(Preflight!=(LONG)0xc0000008) {
  printf("REFUSE|%s|invalid_handle_preflight_status=0x%08lx\n",Stage,(ULONG)Preflight);return;
 }
 __try {Status=CloseNt.Close(Invalid);}
 __except(EXCEPTION_EXECUTE_HANDLER) {Exception=GetExceptionCode();}
 printf("NTCLOSE|%s|preflight_invalid=1|returned=%u|status=0x%08lx|observed_exception=0x%08lx\n",
  Stage,Exception==0,(ULONG)Status,Exception);
 { BOOL Ok=FALSE;DWORD Error=0,PublicException=0;
  SetLastError(0x6b145239);
  __try {Ok=CloseHandle(Invalid);Error=GetLastError();}
  __except(EXCEPTION_EXECUTE_HANDLER) {PublicException=GetExceptionCode();}
  printf("CLOSEHANDLE|%s|ok=%u|last_error=0x%08lx|observed_exception=0x%08lx\n",Stage,Ok,Error,PublicException);
 }
}
int main(int Argc,char **Argv)
{
 HMODULE Native,Kernel;HANDLE Event;LONG ValidStatus;BOOL Remote=FALSE;
 if(Argc!=2 || (strcmp(Argv[1],"baseline") && strcmp(Argv[1],"trace") &&
   strcmp(Argv[1],"checking") && strcmp(Argv[1],"policy") && strcmp(Argv[1],"public-policy"))) return 2;
 if(GetModuleHandleW(L"KexDll.dll")||GetModuleHandleW(L"KxBase.dll")||GetModuleHandleW(L"KxNt.dll")) return 3;
 Native=GetModuleHandleW(L"ntdll.dll");Kernel=GetModuleHandleW(L"kernel32.dll");
 Query.Raw=GetProcAddress(Native,"NtQueryInformationProcess");Set.Raw=GetProcAddress(Native,"NtSetInformationProcess");
 CloseNt.Raw=GetProcAddress(Native,"NtClose");Object.Raw=GetProcAddress(Native,"NtQueryObject");
 Flags.Raw=GetProcAddress(Native,"RtlGetNtGlobalFlags");GetPolicy.Raw=GetProcAddress(Kernel,"GetProcessMitigationPolicy");
 SetPolicy.Raw=GetProcAddress(Kernel,"SetProcessMitigationPolicy");
 if(!OwnedBy(Native,Query)||!OwnedBy(Native,Set)||!OwnedBy(Native,CloseNt)||!OwnedBy(Native,Object)) return 3;
 CheckRemoteDebuggerPresent(GetCurrentProcess(),&Remote);
 printf("BEGIN|case=%s|bits=%u|native_owner=1|kex_absent=1|debugger=%u|remote_debugger=%u|global_flags=0x%08lx\n",
  Argv[1],(UINT)(sizeof(void*)*8),IsDebuggerPresent(),Remote,Flags.Raw?Flags.Flags():0);
 Event=CreateEventW(NULL,TRUE,FALSE,NULL);if(!Event)return 4;
 ValidStatus=CloseNt.Close(Event);printf("VALID_CLOSE|status=0x%08lx\n",(ULONG)ValidStatus);
 QueryState("before",52);QueryState("before",54);
 InvalidClose("before");
 if(!strcmp(Argv[1],"trace")) {
  SetState("trace-enable",32,0,FALSE);QueryState("trace-enabled",32);InvalidClose("trace-enabled");
  SetState("trace-disable",32,0,TRUE);QueryState("trace-disabled",32);InvalidClose("trace-disabled");
 } else if(!strcmp(Argv[1],"checking")) {
  SetState("checking-enable",54,1,FALSE);QueryState("checking-enabled",54);InvalidClose("checking-enabled");
  SetState("checking-disable",54,0,FALSE);QueryState("checking-disabled",54);InvalidClose("checking-disabled");
 } else if(!strcmp(Argv[1],"policy")) {
  SetState("policy-enable",52,3,FALSE);QueryState("policy-enabled",52);InvalidClose("policy-enabled");
  SetState("policy-disable",52,0,FALSE);QueryState("policy-disabled",52);InvalidClose("policy-disabled");
 } else if(!strcmp(Argv[1],"public-policy")) {
  ULONG Value=3;BOOL Ok=FALSE;DWORD Error=0;
  if(!SetPolicy.Raw) printf("PUBLIC_SET|available=0\n");
  else {SetLastError(0x6b145239);Ok=SetPolicy.SetPolicy(3,&Value,sizeof(Value));Error=GetLastError();
   printf("PUBLIC_SET|available=1|ok=%u|last_error=0x%08lx\n",Ok,Error);
   QueryState("public-policy-enabled",52);QueryState("public-policy-enabled",54);InvalidClose("public-policy-enabled");}
 }
 printf("COMPLETE|inventory_only=1|exceptions_observed_from_native_NtClose_only=1|no_system_flags_changed=1\n");
 return 0;
}
