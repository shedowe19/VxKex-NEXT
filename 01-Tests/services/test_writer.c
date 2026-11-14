#include "service_test_shim.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wctype.h>

#define H(n) ((HANDLE)(ULONG_PTR)(n))
#define K(n) ((HKEY)(ULONG_PTR)(n))
static DWORD Error,TransactionCalls,CreateCalls,WriteCalls,CommitCalls,RollbackCalls,CloseCalls,CommittedWrites,StagedWrites,StagedCreates,CommittedCreates;
static int TransactionOpen,KeysOpen,Fault,FailWrite,BaseCount,FilterCount,UseFilterPresent,UseFilterType,UseFilterData,MalformedFilter,ChildCollision,Scenarios;
static const WCHAR *BaseNames[4],*FilterPaths[3];
static KEX_IFEO_PARAMETERS Parameters;
static const WCHAR Exe[]=L"C:\\Vendor\\agent.exe";
static const WCHAR *ParamNames[7]={L"KEX_DisableForChild",L"KEX_DisableAppSpecific",L"KEX_WinVerSpoof",L"KEX_StrongVersionSpoof",L"KEX_DisableConsoleEnhancements",L"KEX_TlsForceEnabledProtocols",L"KEX_TlsForceDisabledProtocols"};
static const ULONG ExpectedValues[7]={0,1,4,2,1,0x800,0x300};
DWORD WINAPI MockGetLastError(VOID){return Error;}
VOID WINAPI MockSetLastError(DWORD value){Error=value;}
BOOLEAN StringEqualI(PCWSTR a,PCWSTR b){while(*a&&*b){if(towlower(*a++)!=towlower(*b++))return FALSE;}return(BOOLEAN)(*a==*b);}
static void Copy(PWSTR out,PCWSTR in){memcpy(out,in,(wcslen(in)+1)*sizeof(WCHAR));}
HRESULT MockStringCchPrintf(PWSTR out,size_t capacity,PCWSTR format,...){
    va_list args;PCWSTR name;ULONG hash;static const WCHAR prefix[]=L"VxKex_Service_";unsigned i;WCHAR hex[9];
    va_start(args,format);
    if(wcsstr(format,L"%s")){name=va_arg(args,PCWSTR);assert(capacity>100);Copy(out,L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\");Copy(out+wcslen(out),name);}
    else{hash=va_arg(args,ULONG);for(i=0;i<8;++i)hex[i]=L"0123456789abcdef"[(hash>>((7-i)*4))&15];hex[8]=0;assert(capacity>wcslen(prefix)+8);Copy(out,prefix);Copy(out+wcslen(out),hex);}
    va_end(args);return S_OK;
}
HANDLE MockCreateSimpleTransaction(PCWSTR description){assert(description&& !TransactionOpen&&!KeysOpen);++TransactionCalls;if(Fault==1){Error=5;return NULL;}TransactionOpen=1;return H(300);}
NTSTATUS WINAPI MockNtCommitTransaction(HANDLE transaction,BOOLEAN wait){assert(transaction==H(300)&&TransactionOpen&&wait);++CommitCalls;if(Fault==8)return(NTSTATUS)0xc0000022U;CommittedWrites=StagedWrites;CommittedCreates=StagedCreates;return 0;}
NTSTATUS WINAPI MockNtRollbackTransaction(HANDLE transaction,BOOLEAN wait){assert(transaction==H(300)&&TransactionOpen&&wait);++RollbackCalls;StagedWrites=StagedCreates=0;return 0;}
NTSTATUS WINAPI MockNtClose(HANDLE transaction){assert(transaction==H(300)&&TransactionOpen&&!KeysOpen);++CloseCalls;TransactionOpen=0;return 0;}
ULONG WINAPI MockRtlNtStatusToDosError(NTSTATUS status){assert(status==(NTSTATUS)0xc0000022U);return ERROR_ACCESS_DENIED;}
LONG WINAPI MockRegCreateKeyTransactedW(HKEY parent,PCWSTR name,DWORD reserved,PWSTR cls,DWORD options,DWORD rights,PVOID security,HKEY *result,LPDWORD disposition,HANDLE transaction,PVOID extra){
    assert(!reserved&&!cls&&!options&&!security&&!extra&&transaction==H(300)&&TransactionOpen);++CreateCalls;
    if(parent==HKEY_LOCAL_MACHINE){assert(rights==(KEY_READ|KEY_WRITE|KEY_WOW64_64KEY));assert(wcsstr(name,L"Image File Execution Options\\agent.exe"));if(Fault==2)return ERROR_ACCESS_DENIED;*result=K(301);}
    else{assert(parent==K(301)&&rights==(KEY_READ|KEY_WRITE)&&wcsstr(name,L"VxKex_Service_")==name&&disposition);if(Fault==7)return ERROR_ACCESS_DENIED;*disposition=ChildCollision?REG_OPENED_EXISTING_KEY:REG_CREATED_NEW_KEY;*result=K(302);++StagedCreates;}
    ++KeysOpen;return 0;
}
LONG WINAPI MockRegEnumValueW(HKEY key,DWORD index,PWSTR name,LPDWORD length,LPDWORD reserved,LPDWORD type,PBYTE data,LPDWORD bytes){
    assert(key==K(301)&&TransactionOpen&&name&&length&&!reserved&&!type&&!data&&!bytes);if(Fault==3)return ERROR_ACCESS_DENIED;if(index>=(DWORD)BaseCount)return ERROR_NO_MORE_ITEMS;assert(*length>wcslen(BaseNames[index]));Copy(name,BaseNames[index]);*length=(DWORD)wcslen(name);return 0;
}
LONG WINAPI MockRegEnumKeyExW(HKEY key,DWORD index,PWSTR name,LPDWORD length,LPDWORD reserved,PWSTR cls,LPDWORD clsLength,PVOID time){
    assert(key==K(301)&&TransactionOpen&&!reserved&&!cls&&!clsLength&&!time);if(Fault==5)return ERROR_ACCESS_DENIED;if(index>=(DWORD)FilterCount)return ERROR_NO_MORE_ITEMS;assert(*length>=2);name[0]=(WCHAR)(L'A'+index);name[1]=0;*length=1;return 0;
}
LONG WINAPI MockRegOpenKeyTransactedW(HKEY parent,PCWSTR name,DWORD options,DWORD rights,HKEY *result,HANDLE transaction,PVOID extra){assert(parent==K(301)&&!options&&rights==KEY_READ&&transaction==H(300)&&!extra);if(Fault==6)return ERROR_ACCESS_DENIED;*result=K(310+name[0]-L'A');++KeysOpen;return 0;}
LONG WINAPI MockRegQueryValueExW(HKEY key,PCWSTR name,LPDWORD reserved,LPDWORD type,PBYTE data,LPDWORD bytes){
    size_t size;unsigned index;assert(TransactionOpen&&!reserved&&type&&data&&bytes);
    if(key==K(301)){assert(!wcscmp(name,L"UseFilter"));if(Fault==4)return ERROR_ACCESS_DENIED;if(!UseFilterPresent)return ERROR_FILE_NOT_FOUND;assert(*bytes>=sizeof(ULONG));*type=(DWORD)UseFilterType;memcpy(data,&UseFilterData,sizeof(ULONG));*bytes=sizeof(ULONG);return 0;}
    index=(unsigned)((ULONG_PTR)key-310);assert(index<(unsigned)FilterCount&&!wcscmp(name,L"FilterFullPath"));size=(wcslen(FilterPaths[index])+1)*sizeof(WCHAR);assert(*bytes>=size+sizeof(WCHAR));memcpy(data,FilterPaths[index],size);*type=REG_SZ;*bytes=(DWORD)size;
    if(MalformedFilter==1)*type=REG_DWORD;
    if(MalformedFilter==2)((WCHAR*)data)[size/sizeof(WCHAR)-1]=L'X';
    if(MalformedFilter==3){((WCHAR*)data)[size/sizeof(WCHAR)]=L'X';((WCHAR*)data)[size/sizeof(WCHAR)+1]=0;*bytes+=(DWORD)(2*sizeof(WCHAR));}
    return 0;
}
LONG WINAPI MockRegSetValueExW(HKEY key,PCWSTR name,DWORD reserved,DWORD type,const BYTE *data,DWORD bytes){
    ULONG value;assert(TransactionOpen&&!reserved&&data);++WriteCalls;if(FailWrite==(int)WriteCalls)return ERROR_ACCESS_DENIED;
    if(key==K(301)){assert(!wcscmp(name,L"UseFilter")&&type==REG_DWORD&&bytes==4);memcpy(&value,data,4);assert(value==1);}
    else{assert(key==K(302));
        if(WriteCalls==1){assert(!wcscmp(name,L"FilterFullPath")&&type==REG_SZ&&bytes==(wcslen(Exe)+1)*sizeof(WCHAR)&&!wcscmp((PCWSTR)data,Exe));}
        else if(WriteCalls==4){assert(!wcscmp(name,L"VerifierDlls")&&type==REG_SZ&&!wcscmp((PCWSTR)data,L"KexDll.dll"));}
        else{assert(type==REG_DWORD&&bytes==4);memcpy(&value,data,4);if(WriteCalls==2)assert(!wcscmp(name,L"GlobalFlag")&&value==FLG_APPLICATION_VERIFIER);else if(WriteCalls==3)assert(!wcscmp(name,L"VerifierFlags")&&value==0x80000000U);else{assert(WriteCalls>=5&&WriteCalls<=11);assert(!wcscmp(name,ParamNames[WriteCalls-5])&&value==ExpectedValues[WriteCalls-5]);}}
    }++StagedWrites;return 0;
}
LONG WINAPI MockRegCloseKey(HKEY key){assert(key==K(301)||key==K(302)||((ULONG_PTR)key>=310&&(ULONG_PTR)key<=312));assert(KeysOpen>0);--KeysOpen;return 0;}
static void Reset(void){assert(!TransactionOpen&&!KeysOpen);Error=77;TransactionCalls=CreateCalls=WriteCalls=CommitCalls=RollbackCalls=CloseCalls=CommittedWrites=StagedWrites=StagedCreates=CommittedCreates=0;Fault=FailWrite=BaseCount=FilterCount=UseFilterPresent=MalformedFilter=ChildCollision=0;UseFilterType=REG_DWORD;UseFilterData=1;Parameters.DisableForChild=0;Parameters.DisableAppSpecific=1;Parameters.WinVerSpoof=WinVerSpoofWin10;Parameters.StrongVersionSpoof=2;Parameters.DisableConsoleEnhancements=1;Parameters.TlsForceEnabledProtocols=0x800;Parameters.TlsForceDisabledProtocols=0x300;}
static void Run(ULONG expected,BOOLEAN expectCreated){BOOLEAN created=TRUE;ULONG actual=KxCfgTryInheritConfiguration(Exe,&Parameters,&created);if(actual!=expected){fprintf(stderr,"writer case=%d fault=%d malformed=%d actual=%lu expected=%lu\n",Scenarios+1,Fault,MalformedFilter,(unsigned long)actual,(unsigned long)expected);exit(1);}assert(actual==expected);assert(created==expectCreated&&!TransactionOpen&&!KeysOpen);if(!expectCreated){assert(!CommittedWrites&&!CommittedCreates);if(TransactionCalls&&Fault!=1)assert(RollbackCalls==1);}else{assert(CommittedWrites==12&&CommittedCreates==1&&CommitCalls==1&&!RollbackCalls);}++Scenarios;}
int main(void){unsigned i;BOOLEAN created;
    Reset();Run(ERROR_SUCCESS,TRUE);
    Reset();BaseNames[0]=L"DisableExceptionChainValidation";BaseCount=1;Run(ERROR_SUCCESS,TRUE);
    Reset();BaseNames[0]=L"UseFilter";BaseCount=1;UseFilterPresent=1;Run(ERROR_SUCCESS,TRUE);
    Reset();BaseNames[0]=L"Debugger";BaseCount=1;Run(ERROR_ALREADY_EXISTS,FALSE);assert(!WriteCalls);
    Reset();BaseNames[0]=L"KEX_WinVerSpoof";BaseCount=1;Run(ERROR_ALREADY_EXISTS,FALSE);
    Reset();UseFilterPresent=1;UseFilterData=0;Run(ERROR_ALREADY_EXISTS,FALSE);
    Reset();UseFilterPresent=1;UseFilterType=REG_SZ;Run(ERROR_ALREADY_EXISTS,FALSE);
    Reset();FilterCount=1;FilterPaths[0]=Exe;UseFilterPresent=1;Run(ERROR_ALREADY_EXISTS,FALSE);assert(!WriteCalls);
    Reset();FilterCount=1;FilterPaths[0]=L"c:\\vendor\\AGENT.EXE";UseFilterPresent=1;Run(ERROR_ALREADY_EXISTS,FALSE);
    Reset();FilterCount=1;FilterPaths[0]=L"C:\\Other\\agent.exe";UseFilterPresent=1;Run(ERROR_SUCCESS,TRUE);
    /* Do not activate dormant filters belonging to other executable paths. */
    Reset();FilterCount=1;FilterPaths[0]=L"C:\\Other\\agent.exe";Run(ERROR_ALREADY_EXISTS,FALSE);
    for(i=1;i<=3;++i){Reset();FilterCount=1;FilterPaths[0]=L"C:\\Other\\agent.exe";UseFilterPresent=1;MalformedFilter=(int)i;Run(ERROR_INVALID_DATA,FALSE);}
    Reset();ChildCollision=1;Run(ERROR_ALREADY_EXISTS,FALSE);
    for(i=1;i<=8;++i){Reset();Fault=(int)i;if(i==6){FilterCount=1;FilterPaths[0]=L"C:\\Other\\agent.exe";UseFilterPresent=1;}Run(ERROR_ACCESS_DENIED,FALSE);}
    for(i=1;i<=12;++i){Reset();FailWrite=(int)i;Run(ERROR_ACCESS_DENIED,FALSE);assert(WriteCalls==i&&!CommitCalls);}
    Reset();Parameters.WinVerSpoof=(KEX_WIN_VER_SPOOF)-1;Run(ERROR_INVALID_PARAMETER,FALSE);assert(!TransactionCalls);
    Reset();Parameters.WinVerSpoof=WinVerSpoofMax;Run(ERROR_INVALID_PARAMETER,FALSE);
    Reset();Parameters.DisableForChild=1;Run(ERROR_INVALID_PARAMETER,FALSE);
    Reset();Parameters.DisableAppSpecific=2;Run(ERROR_INVALID_PARAMETER,FALSE);
    Reset();Parameters.DisableConsoleEnhancements=2;Run(ERROR_INVALID_PARAMETER,FALSE);
    Reset();Parameters.StrongVersionSpoof=4;Run(ERROR_INVALID_PARAMETER,FALSE);
    Reset();created=TRUE;assert(KxCfgTryInheritConfiguration(L"relative.exe",&Parameters,&created)==ERROR_BAD_PATHNAME&&!created&&!TransactionCalls);++Scenarios;
    Reset();assert(KxCfgTryInheritConfiguration(Exe,&Parameters,NULL)==ERROR_INVALID_PARAMETER&&!TransactionCalls);++Scenarios;
    printf("Transactional profile writer: %d actual-function scenarios passed; no registry/transaction APIs executed.\n",Scenarios);return 0;
}
