/* Extend the frozen native-COM suite; the production TU alone uses a GIT
 * forwarding decorator so a lookup/revoke interleaving is deterministic. */
#define main baseline_main
#include "BaselineProbe.c"
#undef main
#include <ObjIdl.h>

typedef struct TRACK_GIT {
    const IGlobalInterfaceTableVtbl *lpVtbl;
    LONG References;
    IGlobalInterfaceTable *Real;
} TRACK_GIT;
static volatile LONG GitObjects,ActiveGets,OverlapRevokes,Revokes,GateEnabled,GateClaimed;
static HANDLE LookupEntered,LookupRelease,ReentrantCloseReturned;
static HANDLE SecondLookupEntered,SecondLookupRelease;
static LONG GateSlots=1;
static INFO *ReentrantInfo;
static HRESULT ReentrantResult;

static ULONG STDMETHODCALLTYPE tAdd(IGlobalInterfaceTable *This)
{return InterlockedIncrement(&((TRACK_GIT*)This)->References);}
static ULONG STDMETHODCALLTYPE tRelease(IGlobalInterfaceTable *This)
{
    TRACK_GIT *Git=(TRACK_GIT*)This;
    ULONG Count=InterlockedDecrement(&Git->References);
    if(!Count){Git->Real->lpVtbl->Release(Git->Real);InterlockedDecrement(&GitObjects);HeapFree(GetProcessHeap(),0,Git);}
    return Count;
}
static HRESULT STDMETHODCALLTYPE tQuery(IGlobalInterfaceTable *This,REFIID Iid,void **Out)
{
    if(!Out)return E_POINTER;*Out=NULL;
    if(IsEqualIID(Iid,&IID_IUnknown)||IsEqualIID(Iid,&IID_IGlobalInterfaceTable)){*Out=This;tAdd(This);return S_OK;}
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE tRegister(IGlobalInterfaceTable *This,IUnknown *Object,REFIID Iid,DWORD *Cookie)
{TRACK_GIT *Git=(TRACK_GIT*)This;return Git->Real->lpVtbl->RegisterInterfaceInGlobal(Git->Real,Object,Iid,Cookie);}
static HRESULT STDMETHODCALLTYPE tRevoke(IGlobalInterfaceTable *This,DWORD Cookie)
{
    TRACK_GIT *Git=(TRACK_GIT*)This;
    LONG InFlight=InterlockedCompareExchange(&ActiveGets,0,0);
    InterlockedIncrement(&Revokes);
    if(InFlight)InterlockedIncrement(&OverlapRevokes);
    return Git->Real->lpVtbl->RevokeInterfaceFromGlobal(Git->Real,Cookie);
}
static HRESULT STDMETHODCALLTYPE tGet(IGlobalInterfaceTable *This,DWORD Cookie,REFIID Iid,void **Out)
{
    TRACK_GIT *Git=(TRACK_GIT*)This;
    HRESULT Result;
    LONG Slot=0;
    InterlockedIncrement(&ActiveGets);
    if(GateEnabled) Slot=InterlockedIncrement(&GateClaimed);
    if(Slot>0&&Slot<=GateSlots){
        SetEvent(Slot==1?LookupEntered:SecondLookupEntered);
        if(ReentrantInfo){ReentrantResult=ReentrantInfo->v->Close(ReentrantInfo);SetEvent(ReentrantCloseReturned);}
        check("bounded deterministic GIT gate",WaitForSingleObject(Slot==1?LookupRelease:SecondLookupRelease,5000)==WAIT_OBJECT_0);
    }
    Result=Git->Real->lpVtbl->GetInterfaceFromGlobal(Git->Real,Cookie,Iid,Out);
    InterlockedDecrement(&ActiveGets);
    return Result;
}
static const IGlobalInterfaceTableVtbl TrackingGitVtbl={tQuery,tAdd,tRelease,tRegister,tRevoke,tGet};

HRESULT WINAPI TestCoCreateInstance(REFCLSID Clsid,IUnknown *Outer,DWORD Context,REFIID Iid,void **Out)
{
    IGlobalInterfaceTable *Real=NULL;
    TRACK_GIT *Git;
    HRESULT Result;
    if(!IsEqualCLSID(Clsid,&CLSID_StdGlobalInterfaceTable)||!IsEqualIID(Iid,&IID_IGlobalInterfaceTable))
        return CoCreateInstance(Clsid,Outer,Context,Iid,Out);
    if(!Out)return E_POINTER;*Out=NULL;
    Result=CoCreateInstance(Clsid,Outer,Context,Iid,(void**)&Real);
    if(FAILED(Result))return Result;
    Git=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*Git));
    if(!Git){Real->lpVtbl->Release(Real);return E_OUTOFMEMORY;}
    Git->lpVtbl=&TrackingGitVtbl;Git->References=1;Git->Real=Real;InterlockedIncrement(&GitObjects);*Out=Git;
    return S_OK;
}
typedef struct GET_WORK {OP *Operation;HRESULT Result;BOOL Present;} GET_WORK;
static DWORD WINAPI GetWorker(void *Parameter)
{
    GET_WORK *Get=Parameter;
    HANDLER *Handler=NULL;
    HRESULT Init=CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if(FAILED(Init)){Get->Result=Init;return 1;}
    Get->Result=Get->Operation->v->Get(Get->Operation,&Handler);
    Get->Present=Handler!=NULL;
    if(Handler)Handler->v->Release(Handler);
    Get->Operation->v->Release(Get->Operation);
    CoUninitialize();return 0;
}
typedef struct RACE_PUT_WORK {WORK Put;HANDLE AfterPut,Finish;} RACE_PUT_WORK;
static DWORD WINAPI RacePutWorker(void *Parameter)
{
    RACE_PUT_WORK *Race=Parameter;
    WORK *Put=&Race->Put;
    OP *Operation=NULL;
    HANDLER *Handler;
    HRESULT Init=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    Put->thread=GetCurrentThreadId();
    if(FAILED(Init)){Put->unmarshaled=Init;SetEvent(Put->ready);SetEvent(Race->AfterPut);return 1;}
    Put->unmarshaled=CoGetInterfaceAndReleaseStream(Put->stream,&AsyncIID,(void**)&Operation);Put->stream=NULL;
    if(SUCCEEDED(Put->unmarshaled)){
        Handler=makeHandler(TRUE,TRUE,0);Put->handler=Handler;
        if(Handler){Handler->ready=Put->ready;Handler->proceed=Put->proceed;Put->put=Operation->v->Put(Operation,Handler);hRelease(Handler);}
        Operation->v->Release(Operation);
    }
    SetEvent(Race->AfterPut);
    check("bounded writer apartment lifetime",WaitForSingleObject(Race->Finish,5000)==WAIT_OBJECT_0);
    CoUninitialize();return 0;
}
static void LookupDetachRace(ULONG Mode)
{
    RACE_PUT_WORK Race;
    WORK *Put=&Race.Put;
    GET_WORK Get;
    GET_WORK SecondGet;
    OP *Operation=makeOperation(TRUE);
    INFO *Info=NULL;
    HANDLE PutThread,Getter;
    HANDLE SecondGetter=NULL;
    LONG BeforeRevokes=Revokes,BeforeOverlap=OverlapRevokes;
    if(!Operation)return;
    ZeroMemory(&Race,sizeof(Race));ZeroMemory(&Get,sizeof(Get));ZeroMemory(&SecondGet,sizeof(SecondGet));
    Put->agile=TRUE;Put->ready=CreateEventW(NULL,TRUE,FALSE,NULL);Put->proceed=CreateEventW(NULL,TRUE,FALSE,NULL);
    Race.AfterPut=CreateEventW(NULL,TRUE,FALSE,NULL);Race.Finish=CreateEventW(NULL,TRUE,FALSE,NULL);
    LookupEntered=CreateEventW(NULL,TRUE,FALSE,NULL);LookupRelease=CreateEventW(NULL,TRUE,FALSE,NULL);ReentrantCloseReturned=CreateEventW(NULL,TRUE,FALSE,NULL);
    SecondLookupEntered=CreateEventW(NULL,TRUE,FALSE,NULL);SecondLookupRelease=CreateEventW(NULL,TRUE,FALSE,NULL);
    check("race info",Operation->v->QI(Operation,&InfoIID,(void**)&Info)==S_OK);
    check("race real FTM stream",CoMarshalInterThreadInterfaceInStream(&AsyncIID,(IUnknown*)Operation,&Put->stream)==S_OK);
    PutThread=CreateThread(NULL,0,RacePutWorker,&Race,0,NULL);
    check("race callback reached",WaitForSingleObject(Put->ready,3000)==WAIT_OBJECT_0&&Put->unmarshaled==S_OK);
    GateClaimed=0;GateSlots=Mode==3?2:1;GateEnabled=TRUE;ReentrantInfo=Mode==1?Info:NULL;ReentrantResult=E_FAIL;
    Get.Operation=Operation;Operation->v->AddRef(Operation);
    Getter=CreateThread(NULL,0,GetWorker,&Get,0,NULL);
    check("GIT lookup entered before close",WaitForSingleObject(LookupEntered,3000)==WAIT_OBJECT_0);
    if(Mode==3){
        SecondGet.Operation=Operation;Operation->v->AddRef(Operation);
        SecondGetter=CreateThread(NULL,0,GetWorker,&SecondGet,0,NULL);
        check("second GIT lookup entered before close",WaitForSingleObject(SecondLookupEntered,3000)==WAIT_OBJECT_0);
    }
    if(Mode==1)check("Close reentry during GIT get does not deadlock",WaitForSingleObject(ReentrantCloseReturned,2000)==WAIT_OBJECT_0&&ReentrantResult==S_OK);
    else if(Mode==0||Mode==3)check("concurrent Close returns with pinned lookup",Info->v->Close(Info)==S_OK);
    else {
        SetEvent(Put->proceed);
        check("completion cleanup returns with pinned lookup",WaitForSingleObject(Race.AfterPut,3000)==WAIT_OBJECT_0&&Put->put==S_OK);
    }
    check("revoke deferred until GIT lookup exits",Revokes==BeforeRevokes);
    check("no Revoke/Get overlap",OverlapRevokes==BeforeOverlap);
    SetEvent(LookupRelease);
    check("getter joins",WaitForSingleObject(Getter,3000)==WAIT_OBJECT_0);
    check("lookup result is handler or closed",(Get.Result==S_OK&&Get.Present)||(Get.Result==ILLEGAL&&!Get.Present));
    if(Mode==3){
        check("first lookup cannot revoke while second lookup is pinned",Revokes==BeforeRevokes&&OverlapRevokes==BeforeOverlap);
        SetEvent(SecondLookupRelease);
        check("second getter joins",WaitForSingleObject(SecondGetter,3000)==WAIT_OBJECT_0);
        check("second lookup has a real result",SecondGet.Result==S_OK&&SecondGet.Present);
        CloseHandle(SecondGetter);
    }
    check("deferred cookie revoked exactly once",Revokes==BeforeRevokes+1&&OverlapRevokes==BeforeOverlap);
    GateEnabled=FALSE;ReentrantInfo=NULL;SetEvent(Put->proceed);
    check("writer completed",WaitForSingleObject(Race.AfterPut,3000)==WAIT_OBJECT_0&&Put->put==S_OK);
    SetEvent(Race.Finish);
    check("put joins",WaitForSingleObject(PutThread,3000)==WAIT_OBJECT_0&&Put->put==S_OK);
    check("callback cleanup does not revoke twice",Revokes==BeforeRevokes+1);
    printf("GIT_RACE|mode=%lu|get_hr=0x%08lx|handler=%u|new_revokes=%ld|new_overlaps=%ld\n",Mode,(ULONG)Get.Result,Get.Present,Revokes-BeforeRevokes,OverlapRevokes-BeforeOverlap);
    CloseHandle(Getter);CloseHandle(PutThread);CloseHandle(Put->ready);CloseHandle(Put->proceed);CloseHandle(Race.AfterPut);CloseHandle(Race.Finish);
    CloseHandle(LookupEntered);CloseHandle(LookupRelease);CloseHandle(ReentrantCloseReturned);
    CloseHandle(SecondLookupEntered);CloseHandle(SecondLookupRelease);
    Info->v->Release(Info);Operation->v->Release(Operation);
}
int __cdecl main(void)
{
    int Result=baseline_main();
    HRESULT Init;
    if(Result)return Result;
    Init=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(Init))return 2;
    LookupDetachRace(0);LookupDetachRace(1);LookupDetachRace(2);LookupDetachRace(3);
    check("all tracked GIT wrappers released",GitObjects==0);
    check("all operation allocations released",allocations==0);
    check("all handlers released",handlers==0);
    printf("OVERLAP_SUMMARY|checks=%ld|failures=%ld|live_allocations=%ld|live_handlers=%ld|live_git=%ld|overlaps=%ld\n",checks,failures,allocations,handlers,GitObjects,OverlapRevokes);
    CoUninitialize();return failures?1:0;
}
