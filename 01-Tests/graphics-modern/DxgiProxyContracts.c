/* Production DXGI outer-proxy contracts; own native objects and no graphics UI. */
#include "../../01-Extended DLLs/KxDx/buildcfg.h"
#include "../../01-Extended DLLs/KxDx/kxdxp.h"
#include <stdio.h>
static LONG Checks,Failures,Live,Allocations,Frees;
static BOOL FailAllocation;
static HANDLE Barrier;
static LONG Arrivals;
#define CHECK(X) do{BOOL ok=!!(X);InterlockedIncrement(&Checks);if(!ok){InterlockedIncrement(&Failures);printf("FAIL|line=%u|%s\n",__LINE__,#X);}}while(0)
static PVOID TestAlloc(HANDLE Heap,DWORD Flags,SIZE_T Size)
{
    PVOID p;if(FailAllocation)return NULL;p=HeapAlloc(Heap,Flags,Size);
    if(p){InterlockedIncrement(&Live);InterlockedIncrement(&Allocations);if(Barrier){if(InterlockedIncrement(&Arrivals)==4)SetEvent(Barrier);if(WaitForSingleObject(Barrier,5000)!=WAIT_OBJECT_0)ExitProcess(88);}}
    return p;
}
static BOOL TestFree(HANDLE Heap,DWORD Flags,PVOID Data)
{BOOL ok=HeapFree(Heap,Flags,Data);if(ok){InterlockedDecrement(&Live);InterlockedIncrement(&Frees);}return ok;}
#undef HeapAlloc
#undef HeapFree
#define HeapAlloc TestAlloc
#define HeapFree TestFree
#include "../../01-Extended DLLs/KxDx/dxgi.c"
#undef HeapAlloc
#undef HeapFree
#include "../../01-Extended DLLs/KxDx/iid.c"
static const IID PrivateIid={0x8d52d826,0x5d66,0x4d46,{0xaa,0xd3,0x81,0xf1,0xb5,0x90,0x88,0x11}};
typedef struct {IDXGIFactory2 Interface;LONG Refs;ULONG Magic;} MOCK_FACTORY;
typedef struct {IDXGIAdapter2 Interface;LONG Refs;ULONG Magic;} MOCK_ADAPTER;
static IDXGIFactory2Vtbl FV;
static IDXGIAdapter2Vtbl AV;
static MOCK_FACTORY NF;
static MOCK_ADAPTER NA;
static LONG PrivateQueries,FactoryMask,AdapterMask;
static BOOL IdentityFailure;
static void CheckF(IDXGIFactory2 *This,UINT Slot)
{CHECK(This==&NF.Interface && NF.Magic==0xF000CAFE && This->lpVtbl==&FV);InterlockedOr(&FactoryMask,1L<<Slot);}
static void CheckA(IDXGIAdapter1 *This,UINT Slot)
{CHECK(This==(IDXGIAdapter1*)&NA.Interface && NA.Magic==0xA000CAFE && This->lpVtbl==&AV.Base);InterlockedOr(&AdapterMask,1L<<Slot);}
static ULONG STDMETHODCALLTYPE FAdd(IDXGIFactory2 *This){CheckF(This,1);return (ULONG)InterlockedIncrement(&NF.Refs);}
static ULONG STDMETHODCALLTYPE FRelease(IDXGIFactory2 *This){LONG n;CheckF(This,2);n=InterlockedDecrement(&NF.Refs);CHECK(n>=0);return (ULONG)n;}
static ULONG STDMETHODCALLTYPE AAdd(IDXGIAdapter1 *This){CheckA(This,1);return (ULONG)InterlockedIncrement(&NA.Refs);}
static ULONG STDMETHODCALLTYPE ARelease(IDXGIAdapter1 *This){LONG n;CheckA(This,2);n=InterlockedDecrement(&NA.Refs);CHECK(n>=0);return (ULONG)n;}
static HRESULT STDMETHODCALLTYPE FQI(IDXGIFactory2 *This,REFIID Iid,PPVOID Object)
{
    CheckF(This,0);*Object=NULL;if(IdentityFailure && IsEqualIID(Iid,&IID_IUnknown))return E_FAIL;
    if(IsEqualIID(Iid,&PrivateIid)){InterlockedIncrement(&PrivateQueries);*Object=This;FAdd(This);return S_OK;}
    if(IsEqualIID(Iid,&IID_IUnknown) || IsEqualIID(Iid,&IID_IDXGIObject) || IsEqualIID(Iid,&IID_IDXGIFactory) || IsEqualIID(Iid,&IID_IDXGIFactory1) || IsEqualIID(Iid,&IID_IDXGIFactory2)){*Object=This;FAdd(This);return S_OK;}
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE AQI(IDXGIAdapter1 *This,REFIID Iid,PPVOID Object)
{
    CheckA(This,0);*Object=NULL;
    if(IsEqualIID(Iid,&PrivateIid)){InterlockedIncrement(&PrivateQueries);*Object=This;AAdd(This);return S_OK;}
    if(IsEqualIID(Iid,&IID_IUnknown) || IsEqualIID(Iid,&IID_IDXGIObject) || IsEqualIID(Iid,&IID_IDXGIAdapter) || IsEqualIID(Iid,&IID_IDXGIAdapter1) || IsEqualIID(Iid,&IID_IDXGIAdapter2)){*Object=This;AAdd(This);return S_OK;}
    return E_NOINTERFACE;
}
#define FM(Name,Slot,Params) static HRESULT STDMETHODCALLTYPE Name Params {CheckF(This,Slot);return S_FALSE;}
#define AM(Name,Slot,Params) static HRESULT STDMETHODCALLTYPE Name Params {CheckA(This,Slot);return S_FALSE;}
FM(FSet,3,(IDXGIFactory2 *This,REFGUID Name,UINT Size,const void *Data))
FM(FSetI,4,(IDXGIFactory2 *This,REFGUID Name,const IUnknown *Object))
FM(FGet,5,(IDXGIFactory2 *This,REFGUID Name,UINT *Size,void *Data))
FM(FParent,6,(IDXGIFactory2 *This,REFIID Iid,PPVOID Parent))
FM(FAssociate,8,(IDXGIFactory2 *This,HWND Window,UINT Flags))
FM(FAssociation,9,(IDXGIFactory2 *This,HWND *Window))
FM(FSwap,10,(IDXGIFactory2 *This,IUnknown *Device,DXGI_SWAP_CHAIN_DESC *Desc,IDXGISwapChain **Swap))
FM(FSwapHwnd,15,(IDXGIFactory2 *This,IUnknown *Device,HWND Window,const DXGI_SWAP_CHAIN_DESC1 *Desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *Full,IDXGIOutput *Output,IDXGISwapChain1 **Swap))
FM(FSwapCore,16,(IDXGIFactory2 *This,IUnknown *Device,IUnknown *Window,const DXGI_SWAP_CHAIN_DESC1 *Desc,IDXGIOutput *Output,IDXGISwapChain1 **Swap))
FM(FResource,17,(IDXGIFactory2 *This,HANDLE Resource,LUID *Luid))
FM(FStereoWindow,18,(IDXGIFactory2 *This,HWND Window,UINT Message,DWORD *Cookie))
FM(FStereoEvent,19,(IDXGIFactory2 *This,HANDLE Event,DWORD *Cookie))
FM(FOcclusionWindow,21,(IDXGIFactory2 *This,HWND Window,UINT Message,DWORD *Cookie))
FM(FOcclusionEvent,22,(IDXGIFactory2 *This,HANDLE Event,DWORD *Cookie))
FM(FComposition,24,(IDXGIFactory2 *This,IUnknown *Device,const DXGI_SWAP_CHAIN_DESC1 *Desc,IDXGIOutput *Output,IDXGISwapChain1 **Swap))
static void STDMETHODCALLTYPE FStereoUnregister(IDXGIFactory2 *This,DWORD Cookie){CheckF(This,20);CHECK(Cookie==17);}
static void STDMETHODCALLTYPE FOcclusionUnregister(IDXGIFactory2 *This,DWORD Cookie){CheckF(This,23);CHECK(Cookie==17);}
static BOOL STDMETHODCALLTYPE FCurrent(IDXGIFactory2 *This){CheckF(This,13);return TRUE;}
static BOOL STDMETHODCALLTYPE FStereo(IDXGIFactory2 *This){CheckF(This,14);return FALSE;}
static HRESULT STDMETHODCALLTYPE FEnum1(IDXGIFactory2 *This,UINT Index,IDXGIAdapter1 **Adapter)
{CheckF(This,12);*Adapter=NULL;if(Index)return DXGI_ERROR_NOT_FOUND;*Adapter=(IDXGIAdapter1*)&NA.Interface;AAdd(*Adapter);return S_OK;}
static HRESULT STDMETHODCALLTYPE FEnum(IDXGIFactory2 *This,UINT Index,IDXGIAdapter **Adapter)
{HRESULT hr;CheckF(This,7);hr=FEnum1(This,Index,(IDXGIAdapter1**)Adapter);return hr;}
static HRESULT STDMETHODCALLTYPE FSoftware(IDXGIFactory2 *This,HMODULE Module,IDXGIAdapter **Adapter)
{CheckF(This,11);CHECK(Module==(HMODULE)17);return FEnum(This,0,Adapter);}
AM(ASet,3,(IDXGIAdapter1 *This,REFGUID Name,UINT Size,const void *Data))
AM(ASetI,4,(IDXGIAdapter1 *This,REFGUID Name,const IUnknown *Object))
AM(AGet,5,(IDXGIAdapter1 *This,REFGUID Name,UINT *Size,void *Data))
AM(AParent,6,(IDXGIAdapter1 *This,REFIID Iid,PPVOID Parent))
AM(AOutputs,7,(IDXGIAdapter1 *This,UINT Index,IDXGIOutput **Output))
AM(ADesc,8,(IDXGIAdapter1 *This,DXGI_ADAPTER_DESC *Desc))
AM(ACheck,9,(IDXGIAdapter1 *This,REFGUID Iid,LARGE_INTEGER *Version))
static HRESULT STDMETHODCALLTYPE ADesc1(IDXGIAdapter1 *This,DXGI_ADAPTER_DESC1 *Desc)
{CheckA(This,10);ZeroMemory(Desc,sizeof(*Desc));Desc->VendorId=0x8086;Desc->AdapterLuid.LowPart=17;return S_OK;}
static HRESULT STDMETHODCALLTYPE ADesc2(IDXGIAdapter2 *This,DXGI_ADAPTER_DESC2 *Desc)
{CheckA((IDXGIAdapter1*)This,11);ZeroMemory(Desc,sizeof(*Desc));Desc->VendorId=0x8086;Desc->AdapterLuid.LowPart=17;return S_OK;}
static void Init(void)
{
    CHECK(Live==0 && KxFactories==NULL && KxAdapters==NULL);
    ZeroMemory(&FV,sizeof(FV));ZeroMemory(&AV,sizeof(AV));
    FV.QueryInterface=FQI;FV.AddRef=FAdd;FV.Release=FRelease;FV.SetPrivateData=FSet;FV.SetPrivateDataInterface=FSetI;FV.GetPrivateData=FGet;FV.GetParent=FParent;FV.EnumAdapters=FEnum;FV.MakeWindowAssociation=FAssociate;FV.GetWindowAssociation=FAssociation;FV.CreateSwapChain=FSwap;FV.CreateSoftwareAdapter=FSoftware;FV.EnumAdapters1=FEnum1;FV.IsCurrent=FCurrent;FV.IsWindowedStereoEnabled=FStereo;FV.CreateSwapChainForHwnd=FSwapHwnd;FV.CreateSwapChainForCoreWindow=FSwapCore;FV.GetSharedResourceAdapterLuid=FResource;FV.RegisterStereoStatusWindow=FStereoWindow;FV.RegisterStereoStatusEvent=FStereoEvent;FV.UnregisterStereoStatus=FStereoUnregister;FV.RegisterOcclusionStatusWindow=FOcclusionWindow;FV.RegisterOcclusionStatusEvent=FOcclusionEvent;FV.UnregisterOcclusionStatus=FOcclusionUnregister;FV.CreateSwapChainForComposition=FComposition;
    AV.Base.QueryInterface=AQI;AV.Base.AddRef=AAdd;AV.Base.Release=ARelease;AV.Base.SetPrivateData=ASet;AV.Base.SetPrivateDataInterface=ASetI;AV.Base.GetPrivateData=AGet;AV.Base.GetParent=AParent;AV.Base.EnumOutputs=AOutputs;AV.Base.GetDesc=ADesc;AV.Base.CheckInterfaceSupport=ACheck;AV.Base.GetDesc1=ADesc1;AV.GetDesc2=ADesc2;
    NF.Interface.lpVtbl=&FV;NF.Refs=1;NF.Magic=0xF000CAFE;NA.Interface.lpVtbl=&AV;NA.Refs=1;NA.Magic=0xA000CAFE;PrivateQueries=FactoryMask=AdapterMask=0;FailAllocation=IdentityFailure=FALSE;
}
static void IdentityAndBase(void)
{
    IDXGIFactory2 *F=&NF.Interface,*Again;IDXGIAdapter1 *A=NULL,*A2=NULL;IDXGIAdapter *A0=NULL;PVOID Object=NULL;HRESULT hr;ULONG Before;UINT Size=4;DWORD Cookie;HWND Window;LUID Luid={17,0};DXGI_ADAPTER_DESC1 Desc;DXGI_ADAPTER_DESC2 Desc2;DXGI_ADAPTER_DESC3 Desc3;LARGE_INTEGER Version;BOOL Tearing=TRUE;
    const IID *FIids[]={&IID_IUnknown,&IID_IDXGIObject,&IID_IDXGIFactory,&IID_IDXGIFactory1,&IID_IDXGIFactory2,&IID_IDXGIFactory3,&IID_IDXGIFactory4,&IID_IDXGIFactory5,&IID_IDXGIFactory6,&IID_IDXGIFactory7};
    const IID *AIids[]={&IID_IUnknown,&IID_IDXGIObject,&IID_IDXGIAdapter,&IID_IDXGIAdapter1,&IID_IDXGIAdapter2,&IID_IDXGIAdapter3,&IID_IDXGIAdapter4};UINT i;
    Init();CHECK(WrapDXGIFactory(&F,2)==S_OK && F!=&NF.Interface && NF.Interface.lpVtbl==&FV && NF.Refs==2);CHECK(WrapDXGIFactory(&F,2)==S_OK && Live==1);
    Again=&NF.Interface;FAdd(Again);CHECK(WrapDXGIFactory(&Again,0)==S_OK && Again==F);CHECK(Again->lpVtbl->Release(Again)==1);
    for(i=0;i<ARRAYSIZE(FIids);++i){Object=NULL;CHECK(F->lpVtbl->QueryInterface(F,FIids[i],&Object)==S_OK && Object==F);((IUnknown*)Object)->lpVtbl->Release(Object);}
    Object=(PVOID)1;CHECK(F->lpVtbl->QueryInterface(F,&PrivateIid,&Object)==E_NOINTERFACE && !Object && PrivateQueries==0);CHECK(F->lpVtbl->QueryInterface(F,&IID_IUnknown,NULL)==E_POINTER);CHECK(F->lpVtbl->QueryInterface(F,NULL,&Object)==E_INVALIDARG && !Object);
    CHECK(F->lpVtbl->SetPrivateData(F,&IID_IUnknown,4,&Size)==S_FALSE);CHECK(F->lpVtbl->SetPrivateDataInterface(F,&IID_IUnknown,NULL)==S_FALSE);CHECK(F->lpVtbl->GetPrivateData(F,&IID_IUnknown,&Size,&Size)==S_FALSE);CHECK(F->lpVtbl->GetParent(F,&IID_IUnknown,&Object)==S_FALSE);
    CHECK(F->lpVtbl->MakeWindowAssociation(F,NULL,0)==S_FALSE);CHECK(F->lpVtbl->GetWindowAssociation(F,&Window)==S_FALSE);CHECK(F->lpVtbl->CreateSwapChain(F,NULL,NULL,NULL)==S_FALSE);CHECK(F->lpVtbl->IsCurrent(F));CHECK(!F->lpVtbl->IsWindowedStereoEnabled(F));
    CHECK(F->lpVtbl->CreateSwapChainForHwnd(F,NULL,NULL,NULL,NULL,NULL,NULL)==S_FALSE);CHECK(F->lpVtbl->CreateSwapChainForCoreWindow(F,NULL,NULL,NULL,NULL,NULL)==S_FALSE);CHECK(F->lpVtbl->GetSharedResourceAdapterLuid(F,NULL,&Luid)==S_FALSE);CHECK(F->lpVtbl->RegisterStereoStatusWindow(F,NULL,17,&Cookie)==S_FALSE);CHECK(F->lpVtbl->RegisterStereoStatusEvent(F,NULL,&Cookie)==S_FALSE);F->lpVtbl->UnregisterStereoStatus(F,17);CHECK(F->lpVtbl->RegisterOcclusionStatusWindow(F,NULL,17,&Cookie)==S_FALSE);CHECK(F->lpVtbl->RegisterOcclusionStatusEvent(F,NULL,&Cookie)==S_FALSE);F->lpVtbl->UnregisterOcclusionStatus(F,17);CHECK(F->lpVtbl->CreateSwapChainForComposition(F,NULL,NULL,NULL,NULL)==S_FALSE);
    CHECK(F->lpVtbl->EnumAdapters1(F,0,&A)==S_OK && A!=(IDXGIAdapter1*)&NA.Interface && NA.Interface.lpVtbl==&AV);CHECK(F->lpVtbl->EnumAdapters1(F,0,&A2)==S_OK && A2==A);A2->lpVtbl->Release(A2);
    CHECK(F->lpVtbl->EnumAdapters(F,0,&A0)==S_OK && A0==(IDXGIAdapter*)A);A0->lpVtbl->Release(A0);CHECK(F->lpVtbl->CreateSoftwareAdapter(F,(HMODULE)17,&A0)==S_OK && A0==(IDXGIAdapter*)A);A0->lpVtbl->Release(A0);
    for(i=0;i<ARRAYSIZE(AIids);++i){Object=NULL;CHECK(A->lpVtbl->QueryInterface(A,AIids[i],&Object)==S_OK && Object==A);((IUnknown*)Object)->lpVtbl->Release(Object);}
    Object=(PVOID)1;CHECK(A->lpVtbl->QueryInterface(A,&PrivateIid,&Object)==E_NOINTERFACE && !Object && PrivateQueries==0);
    CHECK(A->lpVtbl->SetPrivateData(A,&IID_IUnknown,4,&Size)==S_FALSE);CHECK(A->lpVtbl->SetPrivateDataInterface(A,&IID_IUnknown,NULL)==S_FALSE);CHECK(A->lpVtbl->GetPrivateData(A,&IID_IUnknown,&Size,&Size)==S_FALSE);CHECK(A->lpVtbl->EnumOutputs(A,0,NULL)==S_FALSE);CHECK(A->lpVtbl->GetDesc(A,NULL)==S_FALSE);CHECK(A->lpVtbl->CheckInterfaceSupport(A,&IID_IUnknown,&Version)==S_FALSE);CHECK(A->lpVtbl->GetDesc1(A,&Desc)==S_OK && Desc.VendorId==0x8086);
    CHECK(IDXGIAdapter2_GetDesc2((IDXGIAdapter2*)A,&Desc2)==S_OK && Desc2.VendorId==0x8086);CHECK(IDXGIAdapter4_GetDesc3((IDXGIAdapter4*)A,&Desc3)==S_OK && Desc3.VendorId==0x8086);
    CHECK(IDXGIFactory3_GetCreationFlags((IDXGIFactory3*)F)==2);CHECK(IDXGIFactory5_CheckFeatureSupport((IDXGIFactory5*)F,DXGI_FEATURE_PRESENT_ALLOW_TEARING,&Tearing,sizeof(Tearing))==S_OK && !Tearing);
    Luid.LowPart=17;Object=NULL;hr=IDXGIFactory4_EnumAdapterByLuid((IDXGIFactory4*)F,Luid,&IID_IDXGIAdapter4,&Object);CHECK(hr==S_OK && Object==A);if(Object)((IUnknown*)Object)->lpVtbl->Release(Object);
    Object=NULL;CHECK(IDXGIFactory6_EnumAdapterByGpuPreference((IDXGIFactory6*)F,0,DXGI_GPU_PREFERENCE_UNSPECIFIED,&IID_IDXGIAdapter4,&Object)==S_OK && Object==A);if(Object)((IUnknown*)Object)->lpVtbl->Release(Object);
    Object=(PVOID)1;Before=NA.Refs;CHECK(IDXGIFactory6_EnumAdapterByGpuPreference((IDXGIFactory6*)F,0,DXGI_GPU_PREFERENCE_UNSPECIFIED,&PrivateIid,&Object)==E_NOINTERFACE && !Object && NA.Refs==(LONG)Before);
    CHECK((FactoryMask&0x1FFFFFF)==0x1FFFFFF);CHECK((AdapterMask&0xFBF)==0xFBF);
    /* Parent remains the same controlling factory after caller releases it. */
    CHECK(F->lpVtbl->Release(F)==1 && NF.Refs==2);Object=NULL;CHECK(A->lpVtbl->GetParent(A,&IID_IDXGIFactory7,&Object)==S_OK && Object==F);((IUnknown*)Object)->lpVtbl->Release(Object);
    CHECK(A->lpVtbl->Release(A)==0 && NF.Refs==0 && NA.Refs==1);CHECK(ARelease((IDXGIAdapter1*)&NA.Interface)==0);CHECK(Live==0 && KxFactories==NULL && KxAdapters==NULL);
}
static void FailurePaths(void)
{
    IDXGIFactory2 *F;IDXGIAdapter1 *A=(IDXGIAdapter1*)1;PVOID Object=(PVOID)1;LONG Before;LUID Luid={999,0};BOOL Tearing=TRUE;
    Init();F=&NF.Interface;IdentityFailure=TRUE;CHECK(WrapDXGIFactory(&F,0)==E_FAIL && F==&NF.Interface && NF.Refs==1 && Live==0);IdentityFailure=FALSE;
    FailAllocation=TRUE;CHECK(WrapDXGIFactory(&F,0)==E_OUTOFMEMORY && F==&NF.Interface && NF.Refs==1 && NF.Interface.lpVtbl==&FV);FailAllocation=FALSE;
    CHECK(WrapDXGIFactory(&F,0)==S_OK);FailAllocation=TRUE;Before=NA.Refs;CHECK(F->lpVtbl->EnumAdapters1(F,0,&A)==E_OUTOFMEMORY && !A && NA.Refs==Before && NA.Interface.lpVtbl==&AV);FailAllocation=FALSE;
    CHECK(F->lpVtbl->EnumAdapters1(F,99,&A)==DXGI_ERROR_NOT_FOUND && !A);CHECK(F->lpVtbl->EnumAdapters1(F,0,NULL)==E_INVALIDARG);
    CHECK(IDXGIFactory4_EnumAdapterByLuid((IDXGIFactory4*)F,Luid,&IID_IDXGIAdapter4,&Object)==DXGI_ERROR_NOT_FOUND && !Object);Object=(PVOID)1;
    CHECK(IDXGIFactory6_EnumAdapterByGpuPreference((IDXGIFactory6*)F,0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,&IID_IDXGIAdapter1,&Object)==DXGI_ERROR_UNSUPPORTED && !Object);Object=(PVOID)1;
    CHECK(IDXGIFactory6_EnumAdapterByGpuPreference((IDXGIFactory6*)F,0,(DXGI_GPU_PREFERENCE)99,&IID_IDXGIAdapter1,&Object)==E_INVALIDARG && !Object);
    CHECK(IDXGIFactory5_CheckFeatureSupport((IDXGIFactory5*)F,(DXGI_FEATURE)99,&Tearing,sizeof(Tearing))==DXGI_ERROR_INVALID_CALL && Tearing);
    CHECK(F->lpVtbl->Release(F)==0 && NF.Refs==0);CHECK(ARelease((IDXGIAdapter1*)&NA.Interface)==0 && Live==0);
}
typedef struct {IDXGIFactory2 *Owner;UINT Kind;PVOID Result;HRESULT Hr;} WORK;
static DWORD WINAPI Worker(PVOID Parameter)
{
    WORK *Work=Parameter;
    if(!Work->Kind){IDXGIFactory2 *F=&NF.Interface;FAdd(F);Work->Hr=WrapDXGIFactory(&F,0);Work->Result=F;}
    else {IDXGIAdapter1 *A=(IDXGIAdapter1*)&NA.Interface;AAdd(A);Work->Hr=KxWrapAdapter(&A,(KX_DXGI_FACTORY*)Work->Owner);Work->Result=A;}
    return 0;
}
static void Concurrent(void)
{
    HANDLE Threads[4];WORK Work[4];UINT i,Kind;IDXGIFactory2 *Owner;
    Init();Owner=&NF.Interface;
    for(Kind=0;Kind<2;++Kind){
        Barrier=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(Barrier!=NULL);Arrivals=0;
        for(i=0;i<4;++i){Work[i].Kind=Kind;Work[i].Owner=Owner;Work[i].Result=NULL;Threads[i]=CreateThread(NULL,0,Worker,&Work[i],0,NULL);CHECK(Threads[i]!=NULL);}
        CHECK(WaitForMultipleObjects(4,Threads,TRUE,10000)==WAIT_OBJECT_0);CloseHandle(Barrier);Barrier=NULL;
        for(i=0;i<4;++i){CloseHandle(Threads[i]);CHECK(Work[i].Hr==S_OK && Work[i].Result==Work[0].Result);}
        if(!Kind){Owner=Work[0].Result;for(i=1;i<4;++i)((IUnknown*)Work[i].Result)->lpVtbl->Release(Work[i].Result);}
        else for(i=0;i<4;++i)((IUnknown*)Work[i].Result)->lpVtbl->Release(Work[i].Result);
    }
    CHECK(Owner->lpVtbl->Release(Owner)==0 && KxFactories==NULL && KxAdapters==NULL && Live==0);
    CHECK(FRelease(&NF.Interface)==0 && ARelease((IDXGIAdapter1*)&NA.Interface)==0);
}
#ifndef KX_DXGI_NO_MAIN
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);IdentityAndBase();FailurePaths();Concurrent();CHECK(Live==0 && Allocations==Frees);
    printf("%s|dxgi_proxy_contracts|checks=%ld|failures=%ld|live=%ld\n",Failures?"FAIL":"PASS",Checks,Failures,Live);return Failures?1:0;
}
#endif
