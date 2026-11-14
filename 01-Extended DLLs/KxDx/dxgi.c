#include "buildcfg.h"
#include "kxdxp.h"
#include <dxgi.h>

/* Native COM objects and their private dispatch tables are never modified.
 * Each proxy has one controlling IUnknown. Lists own no references. Lookup and
 * final unlink are serialized; no heap/native COM call occurs under the lock. */
typedef struct KX_DXGI_FACTORY KX_DXGI_FACTORY;
typedef struct KX_DXGI_ADAPTER KX_DXGI_ADAPTER;
struct KX_DXGI_FACTORY {
    IDXGIFactory7 Interface; LONG References; IDXGIFactory2 *Native;
    IUnknown *Identity; ULONG CreationFlags; KX_DXGI_FACTORY *Next;
};
struct KX_DXGI_ADAPTER {
    IDXGIAdapter4 Interface; LONG References; IDXGIAdapter1 *Native;
    IUnknown *Identity; KX_DXGI_FACTORY *Owner; KX_DXGI_ADAPTER *Next;
};
static SRWLOCK KxDxgiLock = SRWLOCK_INIT;
static INIT_ONCE KxDxgiOnce = INIT_ONCE_STATIC_INIT;
static KX_DXGI_FACTORY *KxFactories;
static KX_DXGI_ADAPTER *KxAdapters;
static IDXGIFactory7Vtbl KxFactoryVtbl;
static IDXGIAdapter4Vtbl KxAdapterVtbl;
static ULONG STDMETHODCALLTYPE KxFactoryAddRef(IDXGIFactory2 *This);
static ULONG STDMETHODCALLTYPE KxFactoryRelease(IDXGIFactory2 *This);
static ULONG STDMETHODCALLTYPE KxAdapterAddRef(IDXGIAdapter1 *This);
static ULONG STDMETHODCALLTYPE KxAdapterRelease(IDXGIAdapter1 *This);
static HRESULT KxWrapAdapter(IDXGIAdapter1 **Adapter,KX_DXGI_FACTORY *Owner);

static BOOL KxFactoryIid(REFIID Iid)
{
    return IsEqualIID(Iid,&IID_IUnknown) || IsEqualIID(Iid,&IID_IDXGIObject) ||
        IsEqualIID(Iid,&IID_IDXGIFactory) || IsEqualIID(Iid,&IID_IDXGIFactory1) ||
        IsEqualIID(Iid,&IID_IDXGIFactory2) || IsEqualIID(Iid,&IID_IDXGIFactory3) ||
        IsEqualIID(Iid,&IID_IDXGIFactory4) || IsEqualIID(Iid,&IID_IDXGIFactory5) ||
        IsEqualIID(Iid,&IID_IDXGIFactory6) || IsEqualIID(Iid,&IID_IDXGIFactory7);
}
static BOOL KxAdapterIid(REFIID Iid)
{
    return IsEqualIID(Iid,&IID_IUnknown) || IsEqualIID(Iid,&IID_IDXGIObject) ||
        IsEqualIID(Iid,&IID_IDXGIAdapter) || IsEqualIID(Iid,&IID_IDXGIAdapter1) ||
        IsEqualIID(Iid,&IID_IDXGIAdapter2) || IsEqualIID(Iid,&IID_IDXGIAdapter3) ||
        IsEqualIID(Iid,&IID_IDXGIAdapter4);
}
HRESULT STDMETHODCALLTYPE IDXGIFactory7_QueryInterface(IDXGIFactory7 *This,REFIID Iid,PPVOID Object)
{
    if(!Object)return E_POINTER;
    *Object=NULL;if(!Iid)return E_INVALIDARG;
    if(!KxFactoryIid(Iid))return E_NOINTERFACE;
    KxFactoryAddRef((IDXGIFactory2*)This);*Object=This;return S_OK;
}
HRESULT STDMETHODCALLTYPE IDXGIAdapter4_QueryInterface(IDXGIAdapter4 *This,REFIID Iid,PPVOID Object)
{
    if(!Object)return E_POINTER;
    *Object=NULL;if(!Iid)return E_INVALIDARG;
    if(!KxAdapterIid(Iid))return E_NOINTERFACE;
    KxAdapterAddRef((IDXGIAdapter1*)This);*Object=This;return S_OK;
}
static ULONG STDMETHODCALLTYPE KxFactoryAddRef(IDXGIFactory2 *This)
{return (ULONG)InterlockedIncrement(&((KX_DXGI_FACTORY*)This)->References);}
static ULONG STDMETHODCALLTYPE KxAdapterAddRef(IDXGIAdapter1 *This)
{return (ULONG)InterlockedIncrement(&((KX_DXGI_ADAPTER*)This)->References);}
static ULONG STDMETHODCALLTYPE KxFactoryRelease(IDXGIFactory2 *This)
{
    KX_DXGI_FACTORY *Self=(KX_DXGI_FACTORY*)This,**Link;LONG References;
    AcquireSRWLockExclusive(&KxDxgiLock);
    References=InterlockedDecrement(&Self->References);
    if(!References){for(Link=&KxFactories;*Link && *Link!=Self;Link=&(*Link)->Next){}if(*Link)*Link=Self->Next;}
    ReleaseSRWLockExclusive(&KxDxgiLock);
    if(!References){Self->Native->lpVtbl->Release(Self->Native);Self->Identity->lpVtbl->Release(Self->Identity);HeapFree(GetProcessHeap(),0,Self);}
    return (ULONG)References;
}
static ULONG STDMETHODCALLTYPE KxAdapterRelease(IDXGIAdapter1 *This)
{
    KX_DXGI_ADAPTER *Self=(KX_DXGI_ADAPTER*)This,**Link;LONG References;
    AcquireSRWLockExclusive(&KxDxgiLock);
    References=InterlockedDecrement(&Self->References);
    if(!References){for(Link=&KxAdapters;*Link && *Link!=Self;Link=&(*Link)->Next){}if(*Link)*Link=Self->Next;}
    ReleaseSRWLockExclusive(&KxDxgiLock);
    if(!References){Self->Native->lpVtbl->Release(Self->Native);Self->Identity->lpVtbl->Release(Self->Identity);KxFactoryRelease((IDXGIFactory2*)Self->Owner);HeapFree(GetProcessHeap(),0,Self);}
    return (ULONG)References;
}

/* All base methods pass the original native This pointer. */
static HRESULT STDMETHODCALLTYPE KxASetData(IDXGIAdapter1 *This,REFGUID Name,UINT Size,const void *Data)
{IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;return N->lpVtbl->SetPrivateData(N,Name,Size,Data);}
static HRESULT STDMETHODCALLTYPE KxASetInterface(IDXGIAdapter1 *This,REFGUID Name,const IUnknown *Object)
{IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;return N->lpVtbl->SetPrivateDataInterface(N,Name,Object);}
static HRESULT STDMETHODCALLTYPE KxAGetData(IDXGIAdapter1 *This,REFGUID Name,UINT *Size,void *Data)
{IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;return N->lpVtbl->GetPrivateData(N,Name,Size,Data);}
static HRESULT STDMETHODCALLTYPE KxAParent(IDXGIAdapter1 *This,REFIID Iid,PPVOID Parent)
{return IDXGIFactory7_QueryInterface(&((KX_DXGI_ADAPTER*)This)->Owner->Interface,Iid,Parent);}
static HRESULT STDMETHODCALLTYPE KxAEnumOutputs(IDXGIAdapter1 *This,UINT Index,IDXGIOutput **Output)
{IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;return N->lpVtbl->EnumOutputs(N,Index,Output);}
static HRESULT STDMETHODCALLTYPE KxADesc(IDXGIAdapter1 *This,DXGI_ADAPTER_DESC *Desc)
{IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;return N->lpVtbl->GetDesc(N,Desc);}
static HRESULT STDMETHODCALLTYPE KxACheck(IDXGIAdapter1 *This,REFGUID Name,LARGE_INTEGER *Version)
{IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;return N->lpVtbl->CheckInterfaceSupport(N,Name,Version);}
static HRESULT STDMETHODCALLTYPE KxADesc1(IDXGIAdapter1 *This,DXGI_ADAPTER_DESC1 *Desc)
{IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;return N->lpVtbl->GetDesc1(N,Desc);}
HRESULT STDMETHODCALLTYPE IDXGIAdapter2_GetDesc2(IDXGIAdapter2 *This,PDXGI_ADAPTER_DESC2 Desc)
{
    IDXGIAdapter1 *N=((KX_DXGI_ADAPTER*)This)->Native;IDXGIAdapter2 *Newer=NULL;HRESULT Result;
    if(!Desc)return E_INVALIDARG;
    Result=N->lpVtbl->QueryInterface(N,&IID_IDXGIAdapter2,(PPVOID)&Newer);if(FAILED(Result))return Result;
    Result=Newer->lpVtbl->GetDesc2(Newer,Desc);Newer->lpVtbl->Base.Release((IDXGIAdapter1*)Newer);return Result;
}
HRESULT STDMETHODCALLTYPE IDXGIAdapter4_GetDesc3(IDXGIAdapter4 *This,PDXGI_ADAPTER_DESC3 Desc)
{
    DXGI_ADAPTER_DESC2 Native;HRESULT Result;
    C_ASSERT(sizeof(DXGI_ADAPTER_DESC2)==sizeof(DXGI_ADAPTER_DESC3));
    if(!Desc)return E_INVALIDARG;
    Result=IDXGIAdapter2_GetDesc2((IDXGIAdapter2*)This,&Native);if(FAILED(Result))return Result;
    RtlCopyMemory(Desc,&Native,sizeof(*Desc));return S_OK;
}
HRESULT STDMETHODCALLTYPE IDXGIAdapter3_RegisterHardwareContentProtectionTeardownStatusEvent(IDXGIAdapter3 *This,HANDLE Event,PULONG Cookie)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Event);if(Cookie)*Cookie=0;return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IDXGIAdapter3_UnregisterHardwareContentProtectionTeardownStatus(IDXGIAdapter3 *This,ULONG Cookie)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Cookie);return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IDXGIAdapter3_QueryVideoMemoryInfo(IDXGIAdapter3 *This,ULONG Node,DXGI_MEMORY_SEGMENT_GROUP Group,PDXGI_QUERY_VIDEO_MEMORY_INFO Info)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Node);UNREFERENCED_PARAMETER(Group);if(!Info)return E_INVALIDARG;RtlZeroMemory(Info,sizeof(*Info));return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IDXGIAdapter3_SetVideoMemoryReservation(IDXGIAdapter3 *This,ULONG Node,DXGI_MEMORY_SEGMENT_GROUP Group,ULONGLONG Reservation)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Node);UNREFERENCED_PARAMETER(Group);UNREFERENCED_PARAMETER(Reservation);return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IDXGIAdapter3_RegisterVideoMemoryBudgetChangeNotificationEvent(IDXGIAdapter3 *This,HANDLE Event,PULONG Cookie)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Event);if(Cookie)*Cookie=0;return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IDXGIAdapter3_UnregisterVideoMemoryBudgetChangeNotification(IDXGIAdapter3 *This,ULONG Cookie)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Cookie);return E_NOTIMPL;}

/* Consumes an enumeration reference on all paths; output owns one proxy ref. */
static HRESULT KxPublishAdapter(IDXGIAdapter1 *Native,KX_DXGI_FACTORY *Owner,REFIID Iid,PPVOID Adapter)
{
    HRESULT Result=KxWrapAdapter(&Native,Owner);
    if(SUCCEEDED(Result))Result=Native->lpVtbl->QueryInterface(Native,Iid,Adapter);
    Native->lpVtbl->Release(Native);return Result;
}
static HRESULT STDMETHODCALLTYPE KxFSetData(IDXGIFactory2 *This,REFGUID Name,UINT Size,const void *Data)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->SetPrivateData(N,Name,Size,Data);}
static HRESULT STDMETHODCALLTYPE KxFSetInterface(IDXGIFactory2 *This,REFGUID Name,const IUnknown *Object)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->SetPrivateDataInterface(N,Name,Object);}
static HRESULT STDMETHODCALLTYPE KxFGetData(IDXGIFactory2 *This,REFGUID Name,UINT *Size,void *Data)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->GetPrivateData(N,Name,Size,Data);}
static HRESULT STDMETHODCALLTYPE KxFParent(IDXGIFactory2 *This,REFIID Iid,PPVOID Parent)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->GetParent(N,Iid,Parent);}
static HRESULT STDMETHODCALLTYPE KxFEnum1(IDXGIFactory2 *This,UINT Index,IDXGIAdapter1 **Adapter)
{
    KX_DXGI_FACTORY *Self=(KX_DXGI_FACTORY*)This;IDXGIAdapter1 *Native=NULL;HRESULT Result;
    if(!Adapter)return E_INVALIDARG;*Adapter=NULL;
    Result=Self->Native->lpVtbl->EnumAdapters1(Self->Native,Index,&Native);if(FAILED(Result))return Result;
    return KxPublishAdapter(Native,Self,&IID_IDXGIAdapter1,(PPVOID)Adapter);
}
static HRESULT KxPublishBaseAdapter(IDXGIAdapter *Native,KX_DXGI_FACTORY *Owner,IDXGIAdapter **Adapter)
{
    IDXGIAdapter1 *Base=NULL;HRESULT Result;
    Result=Native->lpVtbl->QueryInterface(Native,&IID_IDXGIAdapter1,(PPVOID)&Base);Native->lpVtbl->Release(Native);if(FAILED(Result))return Result;
    return KxPublishAdapter(Base,Owner,&IID_IDXGIAdapter,(PPVOID)Adapter);
}
static HRESULT STDMETHODCALLTYPE KxFEnum(IDXGIFactory2 *This,UINT Index,IDXGIAdapter **Adapter)
{
    KX_DXGI_FACTORY *Self=(KX_DXGI_FACTORY*)This;IDXGIAdapter *Native=NULL;HRESULT Result;
    if(!Adapter)return E_INVALIDARG;*Adapter=NULL;
    Result=Self->Native->lpVtbl->EnumAdapters(Self->Native,Index,&Native);if(FAILED(Result))return Result;
    return KxPublishBaseAdapter(Native,Self,Adapter);
}
static HRESULT STDMETHODCALLTYPE KxFSoftware(IDXGIFactory2 *This,HMODULE Module,IDXGIAdapter **Adapter)
{
    KX_DXGI_FACTORY *Self=(KX_DXGI_FACTORY*)This;IDXGIAdapter *Native=NULL;HRESULT Result;
    if(!Adapter)return E_INVALIDARG;*Adapter=NULL;
    Result=Self->Native->lpVtbl->CreateSoftwareAdapter(Self->Native,Module,&Native);if(FAILED(Result))return Result;
    return KxPublishBaseAdapter(Native,Self,Adapter);
}
static HRESULT STDMETHODCALLTYPE KxFAssociate(IDXGIFactory2 *This,HWND Window,UINT Flags)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->MakeWindowAssociation(N,Window,Flags);}
static HRESULT STDMETHODCALLTYPE KxFAssociation(IDXGIFactory2 *This,HWND *Window)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->GetWindowAssociation(N,Window);}
static HRESULT STDMETHODCALLTYPE KxFSwap(IDXGIFactory2 *This,IUnknown *Device,DXGI_SWAP_CHAIN_DESC *Desc,IDXGISwapChain **Swap)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->CreateSwapChain(N,Device,Desc,Swap);}
static BOOL STDMETHODCALLTYPE KxFCurrent(IDXGIFactory2 *This)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->IsCurrent(N);}
static BOOL STDMETHODCALLTYPE KxFStereo(IDXGIFactory2 *This)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->IsWindowedStereoEnabled(N);}
static HRESULT STDMETHODCALLTYPE KxFSwapHwnd(IDXGIFactory2 *This,IUnknown *Device,HWND Window,const DXGI_SWAP_CHAIN_DESC1 *Desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *Full,IDXGIOutput *Restrict,IDXGISwapChain1 **Swap)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->CreateSwapChainForHwnd(N,Device,Window,Desc,Full,Restrict,Swap);}
static HRESULT STDMETHODCALLTYPE KxFSwapCore(IDXGIFactory2 *This,IUnknown *Device,IUnknown *Window,const DXGI_SWAP_CHAIN_DESC1 *Desc,IDXGIOutput *Restrict,IDXGISwapChain1 **Swap)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->CreateSwapChainForCoreWindow(N,Device,Window,Desc,Restrict,Swap);}
static HRESULT STDMETHODCALLTYPE KxFResourceLuid(IDXGIFactory2 *This,HANDLE Resource,LUID *Luid)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->GetSharedResourceAdapterLuid(N,Resource,Luid);}
static HRESULT STDMETHODCALLTYPE KxFStereoWindow(IDXGIFactory2 *This,HWND Window,UINT Message,DWORD *Cookie)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->RegisterStereoStatusWindow(N,Window,Message,Cookie);}
static HRESULT STDMETHODCALLTYPE KxFStereoEvent(IDXGIFactory2 *This,HANDLE Event,DWORD *Cookie)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->RegisterStereoStatusEvent(N,Event,Cookie);}
static void STDMETHODCALLTYPE KxFStereoUnregister(IDXGIFactory2 *This,DWORD Cookie)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;N->lpVtbl->UnregisterStereoStatus(N,Cookie);}
static HRESULT STDMETHODCALLTYPE KxFOcclusionWindow(IDXGIFactory2 *This,HWND Window,UINT Message,DWORD *Cookie)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->RegisterOcclusionStatusWindow(N,Window,Message,Cookie);}
static HRESULT STDMETHODCALLTYPE KxFOcclusionEvent(IDXGIFactory2 *This,HANDLE Event,DWORD *Cookie)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->RegisterOcclusionStatusEvent(N,Event,Cookie);}
static void STDMETHODCALLTYPE KxFOcclusionUnregister(IDXGIFactory2 *This,DWORD Cookie)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;N->lpVtbl->UnregisterOcclusionStatus(N,Cookie);}
static HRESULT STDMETHODCALLTYPE KxFSwapComposition(IDXGIFactory2 *This,IUnknown *Device,const DXGI_SWAP_CHAIN_DESC1 *Desc,IDXGIOutput *Restrict,IDXGISwapChain1 **Swap)
{IDXGIFactory2 *N=((KX_DXGI_FACTORY*)This)->Native;return N->lpVtbl->CreateSwapChainForComposition(N,Device,Desc,Restrict,Swap);}

ULONG STDMETHODCALLTYPE IDXGIFactory3_GetCreationFlags(IDXGIFactory3 *This)
{return ((KX_DXGI_FACTORY*)This)->CreationFlags;}
HRESULT STDMETHODCALLTYPE IDXGIFactory4_EnumAdapterByLuid(IDXGIFactory4 *This,LUID Luid,REFIID Iid,PPVOID Adapter)
{
    KX_DXGI_FACTORY *Self=(KX_DXGI_FACTORY*)This;IDXGIAdapter1 *Native;DXGI_ADAPTER_DESC1 Desc;HRESULT Result;UINT Index;
    if(!Adapter)return E_INVALIDARG;*Adapter=NULL;if(!Iid)return E_INVALIDARG;
    for(Index=0;;++Index){
        Native=NULL;Result=Self->Native->lpVtbl->EnumAdapters1(Self->Native,Index,&Native);if(FAILED(Result))return Result;
        Result=Native->lpVtbl->GetDesc1(Native,&Desc);
        if(SUCCEEDED(Result) && Desc.AdapterLuid.LowPart==Luid.LowPart && Desc.AdapterLuid.HighPart==Luid.HighPart)return KxPublishAdapter(Native,Self,Iid,Adapter);
        Native->lpVtbl->Release(Native);if(FAILED(Result))return Result;
    }
}
HRESULT STDMETHODCALLTYPE IDXGIFactory4_EnumWarpAdapter(IDXGIFactory4 *This,REFIID Iid,PPVOID Adapter)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Iid);if(!Adapter)return E_INVALIDARG;*Adapter=NULL;return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IDXGIFactory5_CheckFeatureSupport(IDXGIFactory5 *This,DXGI_FEATURE Feature,PVOID Data,ULONG Size)
{
    UNREFERENCED_PARAMETER(This);
    if(Feature!=DXGI_FEATURE_PRESENT_ALLOW_TEARING || !Data || Size!=sizeof(BOOL))return DXGI_ERROR_INVALID_CALL;
    *(BOOL*)Data=FALSE;return S_OK;
}
HRESULT STDMETHODCALLTYPE IDXGIFactory6_EnumAdapterByGpuPreference(IDXGIFactory6 *This,ULONG Index,DXGI_GPU_PREFERENCE Preference,REFIID Iid,PPVOID Adapter)
{
    KX_DXGI_FACTORY *Self=(KX_DXGI_FACTORY*)This;IDXGIAdapter1 *Native=NULL;HRESULT Result;
    if(!Adapter)return E_INVALIDARG;*Adapter=NULL;
    if(!Iid || Preference<DXGI_GPU_PREFERENCE_UNSPECIFIED || Preference>DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)return E_INVALIDARG;
    if(Preference!=DXGI_GPU_PREFERENCE_UNSPECIFIED)return DXGI_ERROR_UNSUPPORTED;
    Result=Self->Native->lpVtbl->EnumAdapters1(Self->Native,Index,&Native);if(FAILED(Result))return Result;
    return KxPublishAdapter(Native,Self,Iid,Adapter);
}
HRESULT STDMETHODCALLTYPE IDXGIFactory7_RegisterAdaptersChangedEvent(IDXGIFactory7 *This,HANDLE Event,PULONG Cookie)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Event);if(Cookie)*Cookie=0;return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IDXGIFactory7_UnregisterAdaptersChangedEvent(IDXGIFactory7 *This,ULONG Cookie)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Cookie);return E_NOTIMPL;}

static BOOL CALLBACK KxInitializeDxgi(PINIT_ONCE Once,PVOID Parameter,PVOID *Context)
{
    IDXGIFactory2Vtbl *F=&KxFactoryVtbl.Base.Base.Base.Base.Base;IDXGIAdapter1Vtbl *A=&KxAdapterVtbl.Base.Base.Base;
    UNREFERENCED_PARAMETER(Once);UNREFERENCED_PARAMETER(Parameter);UNREFERENCED_PARAMETER(Context);
    F->QueryInterface=(HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,REFIID,PPVOID))IDXGIFactory7_QueryInterface;F->AddRef=KxFactoryAddRef;F->Release=KxFactoryRelease;
    F->SetPrivateData=KxFSetData;F->SetPrivateDataInterface=KxFSetInterface;F->GetPrivateData=KxFGetData;F->GetParent=KxFParent;F->EnumAdapters=KxFEnum;F->MakeWindowAssociation=KxFAssociate;F->GetWindowAssociation=KxFAssociation;F->CreateSwapChain=KxFSwap;F->CreateSoftwareAdapter=KxFSoftware;F->EnumAdapters1=KxFEnum1;F->IsCurrent=KxFCurrent;F->IsWindowedStereoEnabled=KxFStereo;
    F->CreateSwapChainForHwnd=KxFSwapHwnd;F->CreateSwapChainForCoreWindow=KxFSwapCore;F->GetSharedResourceAdapterLuid=KxFResourceLuid;F->RegisterStereoStatusWindow=KxFStereoWindow;F->RegisterStereoStatusEvent=KxFStereoEvent;F->UnregisterStereoStatus=KxFStereoUnregister;F->RegisterOcclusionStatusWindow=KxFOcclusionWindow;F->RegisterOcclusionStatusEvent=KxFOcclusionEvent;F->UnregisterOcclusionStatus=KxFOcclusionUnregister;F->CreateSwapChainForComposition=KxFSwapComposition;
    KxFactoryVtbl.Base.Base.Base.Base.GetCreationFlags=IDXGIFactory3_GetCreationFlags;KxFactoryVtbl.Base.Base.Base.EnumAdapterByLuid=IDXGIFactory4_EnumAdapterByLuid;KxFactoryVtbl.Base.Base.Base.EnumWarpAdapter=IDXGIFactory4_EnumWarpAdapter;KxFactoryVtbl.Base.Base.CheckFeatureSupport=IDXGIFactory5_CheckFeatureSupport;KxFactoryVtbl.Base.EnumAdapterByGpuPreference=IDXGIFactory6_EnumAdapterByGpuPreference;KxFactoryVtbl.RegisterAdaptersChangedEvent=IDXGIFactory7_RegisterAdaptersChangedEvent;KxFactoryVtbl.UnregisterAdaptersChangedEvent=IDXGIFactory7_UnregisterAdaptersChangedEvent;
    A->QueryInterface=(HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter1*,REFIID,PPVOID))IDXGIAdapter4_QueryInterface;A->AddRef=KxAdapterAddRef;A->Release=KxAdapterRelease;A->SetPrivateData=KxASetData;A->SetPrivateDataInterface=KxASetInterface;A->GetPrivateData=KxAGetData;A->GetParent=KxAParent;A->EnumOutputs=KxAEnumOutputs;A->GetDesc=KxADesc;A->CheckInterfaceSupport=KxACheck;A->GetDesc1=KxADesc1;
    KxAdapterVtbl.Base.Base.GetDesc2=IDXGIAdapter2_GetDesc2;KxAdapterVtbl.Base.RegisterHardwareContentProtectionTeardownStatusEvent=IDXGIAdapter3_RegisterHardwareContentProtectionTeardownStatusEvent;KxAdapterVtbl.Base.UnregisterHardwareContentProtectionTeardownStatus=IDXGIAdapter3_UnregisterHardwareContentProtectionTeardownStatus;KxAdapterVtbl.Base.QueryVideoMemoryInfo=IDXGIAdapter3_QueryVideoMemoryInfo;KxAdapterVtbl.Base.SetVideoMemoryReservation=IDXGIAdapter3_SetVideoMemoryReservation;KxAdapterVtbl.Base.RegisterVideoMemoryBudgetChangeNotificationEvent=IDXGIAdapter3_RegisterVideoMemoryBudgetChangeNotificationEvent;KxAdapterVtbl.Base.UnregisterVideoMemoryBudgetChangeNotification=IDXGIAdapter3_UnregisterVideoMemoryBudgetChangeNotification;KxAdapterVtbl.GetDesc3=IDXGIAdapter4_GetDesc3;
    return TRUE;
}
/* Success consumes/replaces one native reference. Failure leaves it untouched. */
HRESULT WrapDXGIFactory(IDXGIFactory2 **Factory,ULONG CreationFlags)
{
    IDXGIFactory2 *Native;IUnknown *Identity=NULL;KX_DXGI_FACTORY *Self,*Found;HRESULT Result;
    if(!Factory || !*Factory)return E_INVALIDARG;Native=*Factory;
    if(Native->lpVtbl==&KxFactoryVtbl.Base.Base.Base.Base.Base)return S_OK;
    if(!InitOnceExecuteOnce(&KxDxgiOnce,KxInitializeDxgi,NULL,NULL))return E_UNEXPECTED;
    Result=Native->lpVtbl->QueryInterface(Native,&IID_IUnknown,(PPVOID)&Identity);if(FAILED(Result))return Result;
    Self=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*Self));if(!Self){Identity->lpVtbl->Release(Identity);return E_OUTOFMEMORY;}
    Self->Interface.lpVtbl=&KxFactoryVtbl;Self->References=1;Self->Native=Native;Self->Identity=Identity;Self->CreationFlags=CreationFlags;
    AcquireSRWLockExclusive(&KxDxgiLock);for(Found=KxFactories;Found && Found->Identity!=Identity;Found=Found->Next){}
    if(Found)InterlockedIncrement(&Found->References);else{Self->Next=KxFactories;KxFactories=Self;}
    ReleaseSRWLockExclusive(&KxDxgiLock);
    if(Found){Native->lpVtbl->Release(Native);Identity->lpVtbl->Release(Identity);HeapFree(GetProcessHeap(),0,Self);Self=Found;}
    *Factory=(IDXGIFactory2*)&Self->Interface;return S_OK;
}
static HRESULT KxWrapAdapter(IDXGIAdapter1 **Adapter,KX_DXGI_FACTORY *Owner)
{
    IDXGIAdapter1 *Native;IUnknown *Identity=NULL;KX_DXGI_ADAPTER *Self,*Found;HRESULT Result;
    if(!Adapter || !*Adapter || !Owner)return E_INVALIDARG;Native=*Adapter;
    if(Native->lpVtbl==&KxAdapterVtbl.Base.Base.Base)return S_OK;
    Result=Native->lpVtbl->QueryInterface(Native,&IID_IUnknown,(PPVOID)&Identity);if(FAILED(Result))return Result;
    Self=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*Self));if(!Self){Identity->lpVtbl->Release(Identity);return E_OUTOFMEMORY;}
    Self->Interface.lpVtbl=&KxAdapterVtbl;Self->References=1;Self->Native=Native;Self->Identity=Identity;Self->Owner=Owner;KxFactoryAddRef((IDXGIFactory2*)Owner);
    AcquireSRWLockExclusive(&KxDxgiLock);for(Found=KxAdapters;Found && Found->Identity!=Identity;Found=Found->Next){}
    if(Found)InterlockedIncrement(&Found->References);else{Self->Next=KxAdapters;KxAdapters=Self;}
    ReleaseSRWLockExclusive(&KxDxgiLock);
    if(Found){Native->lpVtbl->Release(Native);Identity->lpVtbl->Release(Identity);KxFactoryRelease((IDXGIFactory2*)Owner);HeapFree(GetProcessHeap(),0,Self);Self=Found;}
    *Adapter=(IDXGIAdapter1*)&Self->Interface;return S_OK;
}

IDXGIAdapter *KxPinNativeDXGIAdapter(IDXGIAdapter *Adapter,PBOOL Pinned)
{
    IDXGIAdapter *Native;
    ULONG LastError;

    *Pinned=FALSE;
    if(!Adapter || Adapter->lpVtbl!=(IDXGIAdapterVtbl*)&KxAdapterVtbl)return Adapter;
    Native=(IDXGIAdapter*)((KX_DXGI_ADAPTER*)Adapter)->Native;
    LastError=GetLastError();
    Native->lpVtbl->AddRef(Native);
    SetLastError(LastError);
    *Pinned=TRUE;
    return Native;
}
