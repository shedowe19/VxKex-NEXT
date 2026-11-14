/* Two native API boundaries, using production proxies and controlled backends. */
#define KX_DXGI_NO_MAIN
#include "DxgiProxyContracts.c"
#include <d3d11.h>

typedef struct {
    IDXGIAdapter *Adapter;D3D_DRIVER_TYPE Type;HMODULE Software;UINT Flags;
    const D3D_FEATURE_LEVEL *Levels;UINT Count,Version;
    const DXGI_SWAP_CHAIN_DESC *Desc;IDXGISwapChain **Swap;
    ID3D11Device **Device;D3D_FEATURE_LEVEL *Chosen;ID3D11DeviceContext **Context;
    HRESULT Result;ULONG EntryError,ResultError;LONG References;
    BOOL Raise,DropOuter;
} EXPECT;
static EXPECT Expected;
static LONG Calls,PinAdds,PinReleases;
static IDXGIAdapter1 *OuterToDrop;
static const ULONG ExceptionCode=0xE042D311;
static ULONG STDMETHODCALLTYPE ChangingAdd(IDXGIAdapter1 *This)
{ULONG r=AAdd(This);InterlockedIncrement(&PinAdds);SetLastError(0xA1112222);return r;}
static ULONG STDMETHODCALLTYPE ChangingRelease(IDXGIAdapter1 *This)
{ULONG r=ARelease(This);InterlockedIncrement(&PinReleases);SetLastError(0xB3334444);return r;}
static void Observe(IDXGIAdapter *Adapter,D3D_DRIVER_TYPE Type,HMODULE Software,UINT Flags,
    const D3D_FEATURE_LEVEL *Levels,UINT Count,UINT Version,ID3D11Device **Device,
    D3D_FEATURE_LEVEL *Chosen,ID3D11DeviceContext **Context)
{
    ULONG Error=GetLastError();
    ++Calls;CHECK(Error==Expected.EntryError);CHECK(Adapter==Expected.Adapter);
    CHECK(Type==Expected.Type && Software==Expected.Software && Flags==Expected.Flags);
    CHECK(Levels==Expected.Levels && Count==Expected.Count && Version==Expected.Version);
    CHECK(Device==Expected.Device && Chosen==Expected.Chosen && Context==Expected.Context);
    CHECK(NA.Refs==Expected.References);
    if(Expected.DropOuter){CHECK(OuterToDrop!=NULL);OuterToDrop->lpVtbl->Release(OuterToDrop);OuterToDrop=NULL;CHECK(NA.Refs>=1);}
    if(Device)*Device=(ID3D11Device*)(ULONG_PTR)0x11223344;
    if(Chosen)*Chosen=D3D_FEATURE_LEVEL_10_1;
    if(Context)*Context=(ID3D11DeviceContext*)(ULONG_PTR)0x55667788;
    SetLastError(Expected.ResultError);
    if(Expected.Raise)RaiseException(ExceptionCode,0,0,NULL);
}
static HRESULT WINAPI MockDevice(IDXGIAdapter *Adapter,D3D_DRIVER_TYPE Type,HMODULE Software,
    UINT Flags,const D3D_FEATURE_LEVEL *Levels,UINT Count,UINT Version,ID3D11Device **Device,
    D3D_FEATURE_LEVEL *Chosen,ID3D11DeviceContext **Context)
{Observe(Adapter,Type,Software,Flags,Levels,Count,Version,Device,Chosen,Context);return Expected.Result;}
static HRESULT WINAPI MockSwap(IDXGIAdapter *Adapter,D3D_DRIVER_TYPE Type,HMODULE Software,
    UINT Flags,const D3D_FEATURE_LEVEL *Levels,UINT Count,UINT Version,const DXGI_SWAP_CHAIN_DESC *Desc,
    IDXGISwapChain **Swap,ID3D11Device **Device,D3D_FEATURE_LEVEL *Chosen,ID3D11DeviceContext **Context)
{
    CHECK(Desc==Expected.Desc && Swap==Expected.Swap);
    if(Swap)*Swap=(IDXGISwapChain*)(ULONG_PTR)0x33445566;
    Observe(Adapter,Type,Software,Flags,Levels,Count,Version,Device,Chosen,Context);return Expected.Result;
}
#define D3D11CreateDevice MockDevice
#define D3D11CreateDeviceAndSwapChain MockSwap
#undef KexLogEvent
#define KexLogEvent(...) ((void)0)
#include "../../01-Extended DLLs/KxDx/dx11.c"
#undef D3D11CreateDevice
#undef D3D11CreateDeviceAndSwapChain

static void Case(UINT Api,UINT Kind,BOOL Failure,BOOL Optional,BOOL Exception,BOOL DropOuter)
{
    IDXGIFactory2 *Factory;IDXGIAdapter1 *Outer;IDXGIAdapter Foreign;IDXGIAdapterVtbl ForeignTable;
    IDXGIAdapter *Argument;ID3D11Device *Device=NULL;ID3D11DeviceContext *Context=NULL;
    IDXGISwapChain *Swap=NULL;D3D_FEATURE_LEVEL Chosen=0,Levels[2]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1};
    DXGI_SWAP_CHAIN_DESC Desc;HRESULT Result=E_UNEXPECTED;BOOL Caught=FALSE;LONG BeforeAdds,BeforeReleases;ULONG Error;
    Init();Factory=&NF.Interface;CHECK(WrapDXGIFactory(&Factory,0)==S_OK);CHECK(Factory->lpVtbl->EnumAdapters1(Factory,0,&Outer)==S_OK);
    ZeroMemory(&ForeignTable,sizeof(ForeignTable));Foreign.lpVtbl=&ForeignTable;
    Argument=Kind==0?(IDXGIAdapter*)Outer:Kind==1?(IDXGIAdapter*)&NA.Interface:Kind==2?&Foreign:NULL;
    ZeroMemory(&Expected,sizeof(Expected));ZeroMemory(&Desc,sizeof(Desc));
    Expected.Adapter=Kind==0?(IDXGIAdapter*)&NA.Interface:Argument;Expected.Type=D3D_DRIVER_TYPE_UNKNOWN;
    Expected.Software=(HMODULE)(ULONG_PTR)0x1234;Expected.Flags=0x51;Expected.Levels=Levels;Expected.Count=2;Expected.Version=17;
    Expected.Desc=&Desc;Expected.Swap=Optional?NULL:&Swap;Expected.Device=Optional?NULL:&Device;Expected.Chosen=Optional?NULL:&Chosen;Expected.Context=Optional?NULL:&Context;
    Expected.Result=Failure?E_ACCESSDENIED:S_FALSE;Expected.EntryError=0x10203040;Expected.ResultError=0x50607080;Expected.References=NA.Refs+(Kind==0?1:0);Expected.Raise=Exception;Expected.DropOuter=DropOuter;
    AV.Base.AddRef=ChangingAdd;AV.Base.Release=ChangingRelease;BeforeAdds=PinAdds;BeforeReleases=PinReleases;OuterToDrop=Outer;
    SetLastError(Expected.EntryError);
    __try {
        if(Api==0)Result=Ext_D3D11CreateDevice(Argument,Expected.Type,Expected.Software,Expected.Flags,Levels,2,17,Expected.Device,Expected.Chosen,Expected.Context);
        else Result=Ext_D3D11CreateDeviceAndSwapChain(Argument,Expected.Type,Expected.Software,Expected.Flags,Levels,2,17,&Desc,Expected.Swap,Expected.Device,Expected.Chosen,Expected.Context);
    } __except(GetExceptionCode()==ExceptionCode?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){Caught=TRUE;}
    Error=GetLastError();CHECK(Caught==Exception);if(!Exception)CHECK(Result==Expected.Result);
    CHECK(Error==Expected.ResultError);CHECK(PinAdds-BeforeAdds==(Kind==0?1:0));
    CHECK(PinReleases-BeforeReleases==(Kind==0?(DropOuter?3:1):0));
    CHECK(NA.Refs==(DropOuter?1:3));CHECK(NF.Interface.lpVtbl==&FV && NA.Interface.lpVtbl==&AV);
    if(!Optional){CHECK(Device==(ID3D11Device*)(ULONG_PTR)0x11223344 && Context==(ID3D11DeviceContext*)(ULONG_PTR)0x55667788 && Chosen==D3D_FEATURE_LEVEL_10_1);if(Api)CHECK(Swap==(IDXGISwapChain*)(ULONG_PTR)0x33445566);}
    else CHECK(!Device && !Context && !Swap && Chosen==0);
    if(!DropOuter)Outer->lpVtbl->Release(Outer);
    CHECK(Factory->lpVtbl->Release(Factory)==0);CHECK(NA.Refs==1 && NF.Refs==0);CHECK(ARelease((IDXGIAdapter1*)&NA.Interface)==0);CHECK(Live==0 && KxFactories==NULL && KxAdapters==NULL);
}
int main(void)
{
    UINT Api,Kind,Failure,Optional;
    setvbuf(stdout,NULL,_IONBF,0);
    for(Api=0;Api<2;++Api)for(Kind=0;Kind<4;++Kind)for(Failure=0;Failure<2;++Failure)for(Optional=0;Optional<2;++Optional)
        Case(Api,Kind,Failure,Optional,FALSE,FALSE);
    for(Api=0;Api<2;++Api){Case(Api,0,FALSE,FALSE,TRUE,FALSE);Case(Api,0,FALSE,FALSE,FALSE,TRUE);}
    CHECK(Calls==36 && Allocations==Frees && Live==0);
    printf("%s|d3d11_bridge_contracts|checks=%ld|failures=%ld|calls=%ld|live=%ld\n",Failures?"FAIL":"PASS",Checks,Failures,Calls,Live);return Failures?1:0;
}
