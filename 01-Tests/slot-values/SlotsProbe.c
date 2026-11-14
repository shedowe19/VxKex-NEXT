/* Owned-process TLS/FLS integration fixture. No registry or network access. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PVOID (WINAPI *GET_SLOT)(DWORD);
typedef struct SLOT_API {
    const char *Name;
    GET_SLOT Old;
    GET_SLOT New;
    DWORD Index;
    DWORD Expanded;
} SLOT_API;

static SLOT_API TlsApi, FlsApi;
static volatile LONG Failures;
static DWORD OwnTls[256];
static DWORD OwnTlsCount;
static LPVOID MainFiber;
static int MainTlsToken, MainFlsToken, ChildTlsToken, ChildFlsToken;
static int FiberTlsToken, FiberFlsToken, ExpandedToken;
static const DWORD Sentinel=0x3ac591e7;

static GET_SLOT Resolve(HMODULE Module, const char *Name)
{
    union { FARPROC Raw; GET_SLOT Slot; } Value;
    Value.Raw=Module ? GetProcAddress(Module,Name) : NULL;
    return Value.Slot;
}

static void Require(BOOL Condition,const char *Name)
{
    printf("{\"event\":\"assert\",\"name\":\"%s\",\"pass\":%s}\n",Name,Condition?"true":"false");
    if (!Condition) InterlockedIncrement(&Failures);
}

static void CompareOne(SLOT_API *Api,const char *Case,DWORD Index,PVOID Expected,DWORD InitialError)
{
    PVOID OldValue=NULL,NewValue=NULL;
    DWORD OldError=0,NewError=0,OldException=0,NewException=0;
    BOOL Match;
    if (!Api->New) return;
    SetLastError(InitialError);
    __try { OldValue=Api->Old(Index); OldError=GetLastError(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { OldException=GetExceptionCode(); }
    SetLastError(InitialError);
    __try { NewValue=Api->New(Index); NewError=GetLastError(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { NewException=GetExceptionCode(); }
    Match=OldException==0 && NewException==0 && OldValue==Expected &&
        NewValue==Expected && NewError==InitialError;
    printf("{\"event\":\"case\",\"api\":\"%s\",\"case\":\"%s\",\"index\":%lu,\"oldValue\":\"%p\",\"newValue\":\"%p\",\"expected\":\"%p\",\"initialError\":%lu,\"oldError\":%lu,\"newError\":%lu,\"oldException\":%lu,\"newException\":%lu,\"pass\":%s}\n",
        Api->Name,Case,Index,OldValue,NewValue,Expected,InitialError,OldError,NewError,OldException,NewException,Match?"true":"false");
    if (!Match) InterlockedIncrement(&Failures);
}

static void Compare(SLOT_API *Api,const char *Case,DWORD Index,PVOID Expected)
{
    CompareOne(Api,Case,Index,Expected,Sentinel);
    CompareOne(Api,Case,Index,Expected,0);
}

static DWORD WINAPI ChildThread(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    Compare(&FlsApi,"fresh-thread-null",FlsApi.Index,NULL);
    Compare(&TlsApi,"fresh-thread-null",TlsApi.Index,NULL);
    Compare(&TlsApi,"fresh-thread-expanded-null",TlsApi.Expanded,NULL);
    Require(TlsSetValue(TlsApi.Index,&ChildTlsToken),"child-tls-set");
    Require(FlsSetValue(FlsApi.Index,&ChildFlsToken),"child-fls-set");
    Compare(&TlsApi,"child-own-value",TlsApi.Index,&ChildTlsToken);
    Compare(&FlsApi,"child-own-value",FlsApi.Index,&ChildFlsToken);
    return 0;
}

static VOID WINAPI ChildFiber(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    Compare(&FlsApi,"fresh-fiber-null",FlsApi.Index,NULL);
    Compare(&TlsApi,"fiber-shares-thread-tls",TlsApi.Index,&MainTlsToken);
    Require(FlsSetValue(FlsApi.Index,&FiberFlsToken),"fiber-fls-set");
    Require(TlsSetValue(TlsApi.Index,&FiberTlsToken),"fiber-tls-set");
    Compare(&FlsApi,"fiber-own-value",FlsApi.Index,&FiberFlsToken);
    SwitchToFiber(MainFiber);
}

int main(int Argc,char **Argv)
{
    HMODULE Module,Core;
    BOOL Native;
    HANDLE Thread;
    DWORD ThreadId,Index;
    LPVOID Fiber;
    if (Argc!=2 || (strcmp(Argv[1],"--native")!=0 && strcmp(Argv[1],"--vxkex")!=0)) {
        printf("Usage: SlotsProbe.exe --native | --vxkex\n");
        return 2;
    }
    Native=strcmp(Argv[1],"--native")==0;
    Core=GetModuleHandleW(L"KexDll.dll");
    if (Native ? Core!=NULL : Core==NULL) {
        printf("FAIL: actual NEXT activation differs from selected mode\n");
        return 2;
    }
    Module=GetModuleHandleW(Native?L"kernel32.dll":L"KxBase.dll");
    TlsApi.Name="TlsGetValue2";
    FlsApi.Name="FlsGetValue2";
    TlsApi.Old=TlsGetValue;
    FlsApi.Old=FlsGetValue;
    TlsApi.New=Resolve(Module,TlsApi.Name);
    FlsApi.New=Resolve(Module,FlsApi.Name);
    printf("{\"event\":\"begin\",\"mode\":\"%s\",\"pointerBits\":%u,\"coreActive\":%s,\"tls2Available\":%s,\"fls2Available\":%s}\n",
        Native?"native":"vxkex",(unsigned int)(sizeof(void*)*8),Core?"true":"false",TlsApi.New?"true":"false",FlsApi.New?"true":"false");
    if (!Module || !TlsApi.New || !FlsApi.New) return 3;
    if (!Native) {
        Require(Resolve(Module,MAKEINTRESOURCEA(1927))==TlsApi.New,"tls2-ordinal-1927-matches-name");
        Require(Resolve(Module,MAKEINTRESOURCEA(1928))==FlsApi.New,"fls2-ordinal-1928-matches-name");
    }
    TlsApi.Index=TlsAlloc();
    if (TlsApi.Index==TLS_OUT_OF_INDEXES) return 4;
    OwnTls[OwnTlsCount++]=TlsApi.Index;
    TlsApi.Expanded=TlsApi.Index;
    while ((TlsApi.Expanded<TLS_MINIMUM_AVAILABLE || TlsApi.Expanded==TlsApi.Index) && OwnTlsCount<256) {
        Index=TlsAlloc();
        if (Index==TLS_OUT_OF_INDEXES) break;
        OwnTls[OwnTlsCount++]=Index;
        TlsApi.Expanded=Index;
    }
    if (TlsApi.Expanded<TLS_MINIMUM_AVAILABLE || TlsApi.Expanded==TlsApi.Index) {
        printf("FAIL: could not allocate a separate owned expansion slot\n");
        for (Index=0;Index<OwnTlsCount;++Index) TlsFree(OwnTls[Index]);
        return 4;
    }
    FlsApi.Index=FlsAlloc(NULL);
    if (FlsApi.Index==FLS_OUT_OF_INDEXES) {
        for (Index=0;Index<OwnTlsCount;++Index) TlsFree(OwnTls[Index]);
        return 4;
    }
    Require(TlsApi.Expanded>=TLS_MINIMUM_AVAILABLE,"own-expanded-tls-slot-allocated");
    Compare(&TlsApi,"allocated-null",TlsApi.Index,NULL);
    Compare(&TlsApi,"expanded-null",TlsApi.Expanded,NULL);
    Compare(&FlsApi,"allocated-null",FlsApi.Index,NULL);
    Compare(&TlsApi,"invalid-index",TLS_OUT_OF_INDEXES,NULL);
    Compare(&FlsApi,"invalid-index",FLS_OUT_OF_INDEXES,NULL);
    Require(TlsSetValue(TlsApi.Index,&MainTlsToken),"main-tls-set");
    Require(TlsSetValue(TlsApi.Expanded,&ExpandedToken),"main-expanded-tls-set");
    Require(FlsSetValue(FlsApi.Index,&MainFlsToken),"main-fls-set");
    Compare(&TlsApi,"main-value",TlsApi.Index,&MainTlsToken);
    Compare(&TlsApi,"expanded-value",TlsApi.Expanded,&ExpandedToken);
    Compare(&FlsApi,"main-value",FlsApi.Index,&MainFlsToken);
    Thread=CreateThread(NULL,0,ChildThread,NULL,0,&ThreadId);
    Require(Thread!=NULL,"child-created");
    if (Thread) {
        DWORD WaitResult=WaitForSingleObject(Thread,10000);
        Require(WaitResult==WAIT_OBJECT_0,"child-finished");
        CloseHandle(Thread);
        if (WaitResult!=WAIT_OBJECT_0) return 5;
    }
    Compare(&TlsApi,"main-after-child",TlsApi.Index,&MainTlsToken);
    Compare(&FlsApi,"main-after-child",FlsApi.Index,&MainFlsToken);
    MainFiber=ConvertThreadToFiber(NULL);
    Require(MainFiber!=NULL,"convert-owned-thread-to-fiber");
    if (MainFiber) {
        Fiber=CreateFiber(65536,ChildFiber,NULL);
        Require(Fiber!=NULL,"owned-fiber-created");
        if (Fiber) {
            SwitchToFiber(Fiber);
            Compare(&FlsApi,"main-fiber-restored",FlsApi.Index,&MainFlsToken);
            Compare(&TlsApi,"tls-shared-after-fiber",TlsApi.Index,&FiberTlsToken);
            DeleteFiber(Fiber);
        }
        Require(ConvertFiberToThread(),"convert-owned-fiber-back-to-thread");
    }
    Require(TlsSetValue(TlsApi.Index,NULL),"main-tls-reset-null");
    Require(FlsSetValue(FlsApi.Index,NULL),"main-fls-reset-null");
    Compare(&TlsApi,"reset-null",TlsApi.Index,NULL);
    Compare(&FlsApi,"reset-null",FlsApi.Index,NULL);
    Require(FlsFree(FlsApi.Index),"own-fls-free");
    for (Index=0;Index<OwnTlsCount;++Index)
        Require(TlsFree(OwnTls[Index]),"own-tls-free");
    printf("{\"event\":\"complete\",\"failures\":%ld,\"fls2Available\":%s,\"tlsSlotsFreed\":%lu}\n",Failures,FlsApi.New?"true":"false",OwnTlsCount);
    return Failures?1:0;
}
