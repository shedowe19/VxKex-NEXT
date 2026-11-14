#include "service_test_shim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>

#define H(n) ((SC_HANDLE)(ULONG_PTR)(n))
enum { WRAPPERS, RESOLVER };
static int Mode,Held,FailAnsi,Allocations,ConfigOpen,FileOpen;
static DWORD Error,NativeError,NativeEntryError,ManagerCalls,OpenCalls,CreateCalls,StartCalls,CloseCalls,InheritanceCalls,WriterCalls,Scenarios;
static SC_HANDLE NativeHandle;
static BOOL NativeBool;
static PVOID ExpectedMachine,ExpectedDatabase,ExpectedName,ExpectedArgv;
static PVOID ExpectedDisplay,ExpectedPath,ExpectedGroup,ExpectedDependencies,ExpectedAccount,ExpectedPassword;
static LPDWORD ExpectedTag;
static SC_HANDLE ExpectedManager,ExpectedService;
static DWORD ExpectedAccess,ExpectedArgc;
static WCHAR InheritedName[257],WriterPath[MAX_PATH];
static KEX_IFEO_PARAMETERS Parameters;
static const KEX_IFEO_PARAMETERS *ExpectedParameters;
static DWORD ResolverFault,StatusValue,TypeValue,StartTypeValue,NeededValue,AttributesValue,WriterResult;
static int ConfigMalformed,WriterCreates,FullPathFault,WindowsFault,FileFault,FinalFault;
static const WCHAR *Command,*FullPathOverride,*FinalPathOverride;
static DWORD StatusCalls,ConfigCalls,FileCloseCalls;
static void CopyWide(PWSTR output,PCWSTR text) { memcpy(output,text,(wcslen(text)+1)*sizeof(WCHAR)); }

DWORD WINAPI MockGetLastError(VOID) { return Error; }
VOID WINAPI MockSetLastError(DWORD value) { Error=value; }
VOID WINAPI MockAcquireExclusive(PSRWLOCK lock) { (void)lock;assert(!Held);Held=1;Error=901; }
VOID WINAPI MockReleaseExclusive(PSRWLOCK lock) { (void)lock;assert(Held);Held=0;Error=902; }
VOID WINAPI MockAcquireShared(PSRWLOCK lock) { MockAcquireExclusive(lock); }
VOID WINAPI MockReleaseShared(PSRWLOCK lock) { MockReleaseExclusive(lock); }
INT WINAPI MockMultiByteToWideChar(unsigned int cp,DWORD flags,LPCSTR text,INT count,LPWSTR output,INT capacity) {
    int i;assert(!Held&&cp==CP_ACP&&!flags&&count>0);Error=903;
    if(FailAnsi||count>capacity)return 0;
    for(i=0;i<count;++i){output[i]=(unsigned char)text[i];}
    return count;
}
SC_HANDLE WINAPI MockOpenSCManagerW(LPCWSTR machine,LPCWSTR database,DWORD access) {
    assert(!Held);++ManagerCalls;
    if(Mode==RESOLVER){assert(!machine&&!database&&access==SC_MANAGER_CONNECT);if(ResolverFault==1){Error=5;return NULL;}++ConfigOpen;Error=904;return H(100);}
    assert((PVOID)machine==ExpectedMachine&&(PVOID)database==ExpectedDatabase&&access==ExpectedAccess);Error=NativeError;return NativeHandle;
}
SC_HANDLE WINAPI MockOpenSCManagerA(LPCSTR machine,LPCSTR database,DWORD access) {
    assert(Mode==WRAPPERS&&!Held);++ManagerCalls;assert((PVOID)machine==ExpectedMachine&&(PVOID)database==ExpectedDatabase&&access==ExpectedAccess);Error=NativeError;return NativeHandle;
}
SC_HANDLE WINAPI MockOpenServiceW(SC_HANDLE manager,LPCWSTR name,DWORD access) {
    assert(!Held);++OpenCalls;
    if(Mode==RESOLVER){assert(manager==H(100)&&!wcscmp(name,L"VendorSvc")&&access==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));if(ResolverFault==2){Error=5;return NULL;}++ConfigOpen;Error=905;return H(101);}
    assert(manager==ExpectedManager&&(PVOID)name==ExpectedName&&access==ExpectedAccess);Error=NativeError;return NativeHandle;
}
SC_HANDLE WINAPI MockOpenServiceA(SC_HANDLE manager,LPCSTR name,DWORD access) {
    assert(Mode==WRAPPERS&&!Held);++OpenCalls;assert(manager==ExpectedManager&&(PVOID)name==ExpectedName&&access==ExpectedAccess);Error=NativeError;return NativeHandle;
}
static void CheckCreate(SC_HANDLE manager,PVOID name,PVOID display,DWORD access,DWORD type,DWORD start,DWORD control,PVOID path,PVOID group,LPDWORD tag,PVOID deps,PVOID account,PVOID password) {
    assert(Mode==WRAPPERS&&!Held);++CreateCalls;assert(manager==ExpectedManager&&name==ExpectedName&&display==ExpectedDisplay&&access==ExpectedAccess);
    assert(type==SERVICE_WIN32_OWN_PROCESS&&start==SERVICE_DEMAND_START&&control==1&&path==ExpectedPath&&group==ExpectedGroup&&tag==ExpectedTag&&deps==ExpectedDependencies&&account==ExpectedAccount&&password==ExpectedPassword);
    if(tag){*tag=42;}
    Error=NativeError;
}
SC_HANDLE WINAPI MockCreateServiceW(SC_HANDLE a,LPCWSTR b,LPCWSTR c,DWORD d,DWORD e,DWORD f,DWORD g,LPCWSTR h,LPCWSTR i,LPDWORD j,LPCWSTR k,LPCWSTR l,LPCWSTR m) { CheckCreate(a,(PVOID)b,(PVOID)c,d,e,f,g,(PVOID)h,(PVOID)i,j,(PVOID)k,(PVOID)l,(PVOID)m);return NativeHandle; }
SC_HANDLE WINAPI MockCreateServiceA(SC_HANDLE a,LPCSTR b,LPCSTR c,DWORD d,DWORD e,DWORD f,DWORD g,LPCSTR h,LPCSTR i,LPDWORD j,LPCSTR k,LPCSTR l,LPCSTR m) { CheckCreate(a,(PVOID)b,(PVOID)c,d,e,f,g,(PVOID)h,(PVOID)i,j,(PVOID)k,(PVOID)l,(PVOID)m);return NativeHandle; }
BOOL WINAPI MockCloseServiceHandle(SC_HANDLE handle) {
    assert(!Held);++CloseCalls;
    if(Mode==RESOLVER){assert(handle==H(100)||handle==H(101));assert(ConfigOpen>0);--ConfigOpen;Error=906;return TRUE;}
    assert(handle==ExpectedService);Error=NativeError;return NativeBool;
}
static BOOL CheckStart(SC_HANDLE handle,DWORD count,PVOID args) {
    assert(Mode==WRAPPERS&&!Held);assert(handle==ExpectedService&&count==ExpectedArgc&&args==ExpectedArgv);assert(Error==NativeEntryError);++StartCalls;Error=NativeError;return NativeBool;
}
BOOL WINAPI MockStartServiceW(SC_HANDLE handle,DWORD count,LPCWSTR *args) { return CheckStart(handle,count,(PVOID)args); }
BOOL WINAPI MockStartServiceA(SC_HANDLE handle,DWORD count,LPCSTR *args) { return CheckStart(handle,count,(PVOID)args); }
VOID WINAPI KxAdvapiTryInheritService(PCWSTR name) { assert(Mode==WRAPPERS&&!Held);++InheritanceCalls;assert(wcslen(name)<ARRAYSIZE(InheritedName));CopyWide(InheritedName,name);Error=999; }
BOOL WINAPI MockQueryServiceStatusEx(SC_HANDLE service,int level,PBYTE output,DWORD bytes,LPDWORD needed) {
    SERVICE_STATUS_PROCESS *status=(SERVICE_STATUS_PROCESS*)output;assert(Mode==RESOLVER&&service==H(101)&&level==SC_STATUS_PROCESS_INFO&&bytes==sizeof(*status));++StatusCalls;
    if(ResolverFault==3){Error=5;return FALSE;}memset(status,0,sizeof(*status));status->dwCurrentState=StatusValue;*needed=sizeof(*status);return TRUE;
}
BOOL WINAPI MockQueryServiceConfigW(SC_HANDLE service,LPQUERY_SERVICE_CONFIGW config,DWORD bytes,LPDWORD needed) {
    WCHAR *text;size_t chars;assert(Mode==RESOLVER&&service==H(101)&&StatusCalls==1);++ConfigCalls;*needed=NeededValue;
    if(config==NULL){assert(bytes==0);if(ResolverFault==4)return TRUE;Error=ResolverFault==5?ERROR_ACCESS_DENIED:ERROR_INSUFFICIENT_BUFFER;return FALSE;}
    assert(bytes==NeededValue&&Allocations==1);if(ResolverFault==6){Error=5;return FALSE;}
    memset(config,0,bytes);config->dwServiceType=TypeValue;config->dwStartType=StartTypeValue;text=(WCHAR*)((BYTE*)config+sizeof(*config));chars=(bytes-sizeof(*config))/sizeof(WCHAR);assert(wcslen(Command)+1<=chars);CopyWide(text,Command);config->lpBinaryPathName=text;
    if(ConfigMalformed==1)config->lpBinaryPathName=(LPWSTR)Command;
    if(ConfigMalformed==2){while(chars--)*text++=L'X';}
    if(ConfigMalformed==3)config->lpBinaryPathName=(LPWSTR)((BYTE*)config+sizeof(*config)+1);
    return TRUE;
}
HLOCAL WINAPI MockLocalAlloc(unsigned int flags,SIZE_T bytes) { PVOID p;assert(Mode==RESOLVER&&flags==LPTR&&bytes==NeededValue);if(ResolverFault==7)return NULL;p=calloc(1,bytes);assert(p);++Allocations;return(HLOCAL)p; }
HLOCAL WINAPI MockLocalFree(HLOCAL value) { assert(value&&Allocations==1);--Allocations;free(value);Error=907;return NULL; }
DWORD WINAPI MockGetFileAttributesW(LPCWSTR path) { assert(Mode==RESOLVER&&path[1]==L':');if(AttributesValue==INVALID_FILE_ATTRIBUTES)Error=5;return AttributesValue; }
static DWORD CopyPath(LPCWSTR from,LPWSTR to,DWORD capacity) { DWORD n=(DWORD)wcslen(from);if(n>=capacity)return n+1;memcpy(to,from,(n+1)*sizeof(WCHAR));return n; }
DWORD WINAPI MockGetFullPathNameW(LPCWSTR path,DWORD capacity,LPWSTR output,LPWSTR *part) { assert(Mode==RESOLVER&&!part);if(FullPathFault){Error=5;return FullPathFault==1?0:capacity;}return CopyPath(FullPathOverride?FullPathOverride:path,output,capacity); }
unsigned int WINAPI MockGetWindowsDirectoryW(LPWSTR output,unsigned int capacity) { assert(Mode==RESOLVER);if(WindowsFault){Error=5;return WindowsFault==1?0:capacity;}return CopyPath(L"C:\\Windows",output,capacity); }
INT WINAPI MockCompareStringOrdinal(LPCWSTR a,INT ac,LPCWSTR b,INT bc,BOOL ignore) { int i;assert(ignore);if(ac<0)ac=(INT)wcslen(a);if(bc<0)bc=(INT)wcslen(b);if(ac!=bc)return ac<bc?CSTR_LESS_THAN:CSTR_GREATER_THAN;for(i=0;i<ac;++i){wint_t x=towlower(a[i]),y=towlower(b[i]);if(x!=y)return x<y?CSTR_LESS_THAN:CSTR_GREATER_THAN;}return CSTR_EQUAL; }
HANDLE WINAPI MockCreateFileW(LPCWSTR path,DWORD access,DWORD sharing,PVOID security,DWORD disposition,DWORD flags,HANDLE templateFile) { assert(Mode==RESOLVER&&path[1]==L':'&&access==FILE_READ_ATTRIBUTES&&sharing==FILE_SHARE_READ&&!security&&disposition==OPEN_EXISTING&&flags==FILE_ATTRIBUTE_NORMAL&&!templateFile&&!FileOpen);if(FileFault){Error=5;return INVALID_HANDLE_VALUE;}FileOpen=1;return(HANDLE)H(900); }
DWORD WINAPI MockGetFinalPathNameByHandleW(HANDLE file,LPWSTR output,DWORD capacity,DWORD flags) { assert(file==(HANDLE)H(900)&&FileOpen&&flags==(FILE_NAME_NORMALIZED|VOLUME_NAME_DOS));if(FinalFault){Error=5;return FinalFault==1?0:capacity;}return CopyPath(FinalPathOverride?FinalPathOverride:L"\\\\?\\C:\\Vendor\\agent.exe",output,capacity); }
BOOL WINAPI MockCloseHandle(HANDLE handle) { assert(handle==(HANDLE)H(900)&&FileOpen);FileOpen=0;++FileCloseCalls;Error=908;return TRUE; }
ULONG KXCFGAPI KxCfgTryInheritConfiguration(PCWSTR path,const KEX_IFEO_PARAMETERS *parameters,PBOOLEAN created) { assert(Mode==RESOLVER&&parameters==ExpectedParameters&&FileOpen);assert(!memcmp(parameters,&Parameters,sizeof(Parameters)));++WriterCalls;CopyWide(WriterPath,path);*created=(BOOLEAN)WriterCreates;Error=909;return WriterResult; }

static void Reset(int mode) {
    assert(!Held&&!Allocations&&!ConfigOpen&&!FileOpen);Mode=mode;KxAdvapiResetServiceTrackingForTest();Error=111;NativeError=222;NativeEntryError=333;NativeHandle=H(10);NativeBool=0x100;
    ExpectedMachine=ExpectedDatabase=ExpectedName=ExpectedArgv=NULL;ExpectedManager=H(10);ExpectedService=H(20);ExpectedAccess=SERVICE_START;ExpectedArgc=0;
    ExpectedDisplay=ExpectedPath=ExpectedGroup=ExpectedDependencies=ExpectedAccount=ExpectedPassword=NULL;ExpectedTag=NULL;
    ManagerCalls=OpenCalls=CreateCalls=StartCalls=CloseCalls=InheritanceCalls=WriterCalls=0;FailAnsi=0;InheritedName[0]=WriterPath[0]=0;
    ResolverFault=ConfigMalformed=FullPathFault=WindowsFault=FileFault=FinalFault=0;StatusValue=SERVICE_STOPPED;TypeValue=SERVICE_WIN32_OWN_PROCESS;StartTypeValue=SERVICE_DEMAND_START;NeededValue=4096;AttributesValue=FILE_ATTRIBUTE_NORMAL;WriterResult=0;WriterCreates=1;StatusCalls=ConfigCalls=FileCloseCalls=0;
    Command=L"\"C:\\Vendor\\agent.exe\" /svc";FullPathOverride=FinalPathOverride=NULL;
    Parameters.DisableForChild=0;Parameters.DisableAppSpecific=1;Parameters.WinVerSpoof=4;Parameters.StrongVersionSpoof=2;Parameters.DisableConsoleEnhancements=1;Parameters.TlsForceEnabledProtocols=0x800;Parameters.TlsForceDisabledProtocols=0x300;ExpectedParameters=&Parameters;
}
static SC_HANDLE ManagerW(LPCWSTR machine,LPCWSTR database) { ExpectedMachine=(PVOID)machine;ExpectedDatabase=(PVOID)database;assert(Ext_OpenSCManagerW(machine,database,ExpectedAccess)==NativeHandle&&Error==NativeError);++Scenarios;return NativeHandle; }
static void ServiceW(SC_HANDLE manager,LPCWSTR name,DWORD access,SC_HANDLE result) { ExpectedManager=manager;ExpectedName=(PVOID)name;ExpectedAccess=access;NativeHandle=result;assert(Ext_OpenServiceW(manager,name,access)==result&&Error==NativeError);++Scenarios; }
static void StartW(SC_HANDLE service,DWORD expectedInheritance) { ExpectedService=service;Error=NativeEntryError;assert(Ext_StartServiceW(service,ExpectedArgc,(LPCWSTR*)ExpectedArgv)==NativeBool&&Error==NativeError);assert(InheritanceCalls==expectedInheritance);++Scenarios; }
static void Resolver(ULONG expected,BOOL expectedCreated) { BOOLEAN created=TRUE;assert(KxCfgTryInheritServiceParameters(L"VendorSvc",&Parameters,&created)==expected);assert(created==expectedCreated&&!Allocations&&!ConfigOpen&&!FileOpen);++Scenarios; }
static void TestWrappers(void) {
    unsigned i;DWORD tag;SC_HANDLE manager;LPCSTR ansiName="AnsiSvc",createdAnsi="CreatedAnsi";LPCWSTR createdWide=L"CreatedSvc";LPCWSTR wideArgs[2]={L"VendorSvc",L"wide argument"};LPCSTR ansiArgs[2]={"VendorSvc","ansi argument"};WCHAR longName[258];char longAnsi[1026];
    Reset(WRAPPERS);StartW(H(777),0);assert(StartCalls==1);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ServiceW(manager,L"VendorSvc",SERVICE_START,H(20));ExpectedArgv=(PVOID)wideArgs;ExpectedArgc=2;StartW(H(20),1);assert(!wcscmp(InheritedName,L"VendorSvc")&&StartCalls==1);
    Reset(WRAPPERS);manager=ManagerW(L"remote",NULL);ServiceW(manager,L"VendorSvc",SERVICE_START,H(20));StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,L"ServicesFailed");ServiceW(manager,L"VendorSvc",SERVICE_START,H(20));StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(L"",L"servicesactive");ServiceW(manager,L"VendorSvc",GENERIC_EXECUTE,H(20));StartW(H(20),1);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ServiceW(manager,L"VendorSvc",GENERIC_ALL,H(20));StartW(H(20),1);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ServiceW(manager,L"VendorSvc",MAXIMUM_ALLOWED,H(20));StartW(H(20),0);
    Reset(WRAPPERS);ServiceW(H(777),L"VendorSvc",SERVICE_START,H(20));StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ServiceW(manager,L"VendorSvc",SERVICE_QUERY_CONFIG,H(20));StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ServiceW(manager,L"VendorSvc",SERVICE_START,H(20));ExpectedService=H(20);assert(Ext_CloseServiceHandle(H(20))==NativeBool&&Error==NativeError);StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ServiceW(manager,L"VendorSvc",SERVICE_START,H(20));ExpectedService=H(20);NativeBool=FALSE;assert(!Ext_CloseServiceHandle(H(20)));StartW(H(20),1);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ServiceW(manager,L"VendorSvc",SERVICE_START,H(20));NativeHandle=H(20);ManagerW(L"remote",NULL);StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);for(i=0;i<257;++i)longName[i]=L'X';longName[257]=0;ServiceW(manager,longName,SERVICE_START,H(20));StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);for(i=0;i<130;++i)ServiceW(manager,L"VendorSvc",SERVICE_START,H(1000+i));StartW(H(1129),0);StartW(H(1000),1);
    Reset(WRAPPERS);ExpectedMachine=NULL;ExpectedDatabase=NULL;ExpectedAccess=1;assert(Ext_OpenSCManagerA(NULL,NULL,1)==H(10)&&Error==NativeError);ExpectedManager=H(10);ExpectedName=(PVOID)ansiName;ExpectedAccess=SERVICE_START;NativeHandle=H(20);assert(Ext_OpenServiceA(H(10),ansiName,SERVICE_START)==H(20)&&Error==NativeError);ExpectedService=H(20);ExpectedArgv=(PVOID)ansiArgs;ExpectedArgc=2;Error=NativeEntryError;assert(Ext_StartServiceA(H(20),2,ansiArgs)==NativeBool&&Error==NativeError&&InheritanceCalls==1&&!wcscmp(InheritedName,L"AnsiSvc"));++Scenarios;
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ExpectedManager=manager;ExpectedName=(PVOID)ansiName;ExpectedAccess=SERVICE_START;NativeHandle=H(20);FailAnsi=1;assert(Ext_OpenServiceA(manager,ansiName,SERVICE_START)==H(20)&&Error==NativeError);StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);memset(longAnsi,'X',sizeof(longAnsi));longAnsi[1025]=0;ExpectedManager=manager;ExpectedName=longAnsi;ExpectedAccess=SERVICE_START;NativeHandle=H(20);assert(Ext_OpenServiceA(manager,longAnsi,SERVICE_START)==H(20));StartW(H(20),0);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ExpectedManager=manager;ExpectedName=(PVOID)createdWide;ExpectedAccess=SERVICE_START;NativeHandle=H(30);assert(Ext_CreateServiceW(manager,createdWide,NULL,SERVICE_START,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,1,NULL,NULL,NULL,NULL,NULL,NULL)==H(30));assert(Error==NativeError&&InheritanceCalls==1&&CreateCalls==1);StartW(H(30),2);
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ExpectedManager=manager;ExpectedName=(PVOID)createdAnsi;ExpectedAccess=SERVICE_START;NativeHandle=H(30);assert(Ext_CreateServiceA(manager,createdAnsi,NULL,SERVICE_START,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,1,NULL,NULL,NULL,NULL,NULL,NULL)==H(30));assert(Error==NativeError&&InheritanceCalls==1&&CreateCalls==1);++Scenarios;
    Reset(WRAPPERS);manager=ManagerW(L"remote",NULL);ExpectedManager=manager;ExpectedName=(PVOID)createdWide;ExpectedAccess=SERVICE_START;NativeHandle=H(30);assert(Ext_CreateServiceW(manager,createdWide,NULL,SERVICE_START,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,1,NULL,NULL,NULL,NULL,NULL,NULL)==H(30));assert(Error==NativeError&&!InheritanceCalls);++Scenarios;
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ExpectedManager=manager;ExpectedName=(PVOID)createdWide;ExpectedAccess=SERVICE_START;NativeHandle=H(30);ExpectedDisplay=(PVOID)L"Display";ExpectedPath=(PVOID)L"C:\\Vendor\\agent.exe";ExpectedGroup=(PVOID)L"Group";ExpectedDependencies=(PVOID)L"Dependency\0";ExpectedAccount=(PVOID)L"FixtureAccount";ExpectedPassword=(PVOID)L"fixture-only";ExpectedTag=&tag;tag=0;assert(Ext_CreateServiceW(manager,createdWide,(LPCWSTR)ExpectedDisplay,SERVICE_START,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,1,(LPCWSTR)ExpectedPath,(LPCWSTR)ExpectedGroup,&tag,(LPCWSTR)ExpectedDependencies,(LPCWSTR)ExpectedAccount,(LPCWSTR)ExpectedPassword)==H(30));assert(tag==42&&Error==NativeError&&InheritanceCalls==1);++Scenarios;
    Reset(WRAPPERS);manager=ManagerW(NULL,NULL);ExpectedManager=manager;ExpectedName=(PVOID)createdAnsi;ExpectedAccess=SERVICE_START;NativeHandle=H(30);ExpectedDisplay=(PVOID)"Display";ExpectedPath=(PVOID)"C:\\Vendor\\agent.exe";ExpectedGroup=(PVOID)"Group";ExpectedDependencies=(PVOID)"Dependency\0";ExpectedAccount=(PVOID)"FixtureAccount";ExpectedPassword=(PVOID)"fixture-only";ExpectedTag=&tag;tag=0;assert(Ext_CreateServiceA(manager,createdAnsi,(LPCSTR)ExpectedDisplay,SERVICE_START,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,1,(LPCSTR)ExpectedPath,(LPCSTR)ExpectedGroup,&tag,(LPCSTR)ExpectedDependencies,(LPCSTR)ExpectedAccount,(LPCSTR)ExpectedPassword)==H(30));assert(tag==42&&Error==NativeError&&InheritanceCalls==1);++Scenarios;
    Reset(WRAPPERS);NativeHandle=NULL;ManagerW(NULL,NULL);assert(!InheritanceCalls);StartW(H(20),0);
}
static void TestResolver(void) {
    unsigned i;static const WCHAR *invalid[]={L"",L"relative.exe",L"C:agent.exe",L"C:\\Program Files\\agent.exe /svc",L"\"C:\\Vendor\\agent.exe\"bad",L"%SystemRoot%\\agent.exe",L"C:\\Vendor\\agent.dll",L"\\\\host\\share\\agent.exe",L"C:\\Vendor\\agent.exe /arg=%TEMP%",L"C:\\Vendor\\agent.exe:stream.exe"};
    assert(sizeof(KEX_IFEO_PARAMETERS)==28);
    Reset(RESOLVER);Resolver(ERROR_SUCCESS,TRUE);assert(WriterCalls==1&&!wcscmp(WriterPath,L"C:\\Vendor\\agent.exe"));
    Reset(RESOLVER);WriterCreates=0;Resolver(ERROR_SUCCESS,FALSE);assert(WriterCalls==1);
    Reset(RESOLVER);WriterResult=ERROR_ACCESS_DENIED;Resolver(ERROR_ACCESS_DENIED,FALSE);assert(WriterCalls==1);
    for(i=1;i<=7;++i){Reset(RESOLVER);ResolverFault=i;Resolver(i==4?ERROR_INVALID_DATA:i==7?ERROR_NOT_ENOUGH_MEMORY:ERROR_ACCESS_DENIED,FALSE);assert(!WriterCalls);}
    Reset(RESOLVER);StatusValue=SERVICE_RUNNING;Resolver(ERROR_SERVICE_ALREADY_RUNNING,FALSE);assert(!ConfigCalls&&!WriterCalls);
    Reset(RESOLVER);StatusValue=SERVICE_START_PENDING;Resolver(ERROR_SERVICE_ALREADY_RUNNING,FALSE);
    Reset(RESOLVER);TypeValue=SERVICE_WIN32_SHARE_PROCESS;Resolver(ERROR_NOT_SUPPORTED,FALSE);
    Reset(RESOLVER);TypeValue=SERVICE_KERNEL_DRIVER;Resolver(ERROR_NOT_SUPPORTED,FALSE);
    Reset(RESOLVER);TypeValue=SERVICE_WIN32_OWN_PROCESS|SERVICE_INTERACTIVE_PROCESS;Resolver(ERROR_SUCCESS,TRUE);
    Reset(RESOLVER);StartTypeValue=SERVICE_DISABLED;Resolver(ERROR_SERVICE_DISABLED,FALSE);
    Reset(RESOLVER);NeededValue=8193;Resolver(ERROR_INVALID_DATA,FALSE);
    Reset(RESOLVER);NeededValue=1;Resolver(ERROR_INVALID_DATA,FALSE);
    for(i=1;i<=3;++i){Reset(RESOLVER);ConfigMalformed=(int)i;Resolver(ERROR_INVALID_DATA,FALSE);assert(!WriterCalls);}
    for(i=0;i<ARRAYSIZE(invalid);++i){Reset(RESOLVER);Command=invalid[i];Resolver(ERROR_INVALID_NAME,FALSE);assert(!WriterCalls);}
    Reset(RESOLVER);Command=L"C:\\Vendor\\agent.EXE /svc";Resolver(ERROR_SUCCESS,TRUE);
    Reset(RESOLVER);Command=L"\"C:\\Vendor\\agent.exe\"\t/svc";Resolver(ERROR_SUCCESS,TRUE);
    Reset(RESOLVER);Command=L"C:\\Windows\\System32\\svchost.exe -k test";Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);FullPathOverride=L"c:\\WINDOWS\\agent.exe";Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);FullPathOverride=L"C:\\WindowsBackup\\agent.exe";Resolver(ERROR_SUCCESS,TRUE);
    Reset(RESOLVER);FullPathFault=1;Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);FullPathFault=2;Resolver(ERROR_FILENAME_EXCED_RANGE,FALSE);
    Reset(RESOLVER);WindowsFault=1;Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);WindowsFault=2;Resolver(ERROR_FILENAME_EXCED_RANGE,FALSE);
    Reset(RESOLVER);AttributesValue=INVALID_FILE_ATTRIBUTES;Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);AttributesValue=FILE_ATTRIBUTE_DIRECTORY;Resolver(ERROR_DIRECTORY,FALSE);
    Reset(RESOLVER);FileFault=1;Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);FinalFault=1;Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);FinalPathOverride=L"\\\\?\\C:\\Windows\\System32\\svchost.exe";Resolver(ERROR_ACCESS_DENIED,FALSE);
    Reset(RESOLVER);FinalPathOverride=L"\\\\?\\UNC\\host\\share\\agent.exe";Resolver(ERROR_INVALID_NAME,FALSE);
}
int main(void) { TestWrappers();TestResolver();printf("Service propagation: %lu actual-function scenarios passed; all OS calls doubled.\n",(unsigned long)Scenarios);return 0; }
