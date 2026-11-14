// Runtime contract fixture plus independent outer-D2D and backend mock checks.
// Physical --installed checks remain mandatory; mocks are not GPU evidence.
#define main D2DProxyMockMain
#include "D2DProxyContracts.c"
#undef main
#undef CHECK
#include <d3d11.h>
// This combined fixture exercises D2D and unavailable-backend contracts only.
// Its source-included D3D11 creation thunks are not a bridge test. Any accidental
// call must stop before reaching a real GPU API; D3D11BridgeContracts.c separately
// includes the real production pin helper and tests both boundaries mandatorily.
IDXGIAdapter *KxPinNativeDXGIAdapter(IDXGIAdapter *Adapter,PBOOL Pinned)
{
    UNREFERENCED_PARAMETER(Adapter);UNREFERENCED_PARAMETER(Pinned);
    puts("FAIL|unexpected adapter bridge call in D2D/backend-only fixture");
    ExitProcess(87);
}
#include "../../01-Extended DLLs/KxDx/dx11.c"
#include "../../01-Extended DLLs/KxDx/dcmpstub.c"
#include "../../01-Extended DLLs/KxDx/dx12stub.c"
DEFINE_GUID(IID_IDXGIDevice,0x54ec77fa,0x1377,0x44e6,0x8c,0x32,0x88,0xfd,0x5f,0x44,0xc8,0x4c);
static UINT Assertions,RuntimeFailures;
#define CHECK(Expression) do {++Assertions;if(!(Expression)) {++RuntimeFailures;printf("FAIL|line=%u|%s\n",__LINE__,#Expression);}} while(0)
static HRESULT NativeFactoryForReference(REFIID RefIID,PPVOID Factory)
{return CreateDXGIFactory1(RefIID,Factory);}
static void CheckUnsupportedBackends(void)
{
    ID3D11Device *Device=(ID3D11Device*)1;
    ID3D11DeviceContext *Context=(ID3D11DeviceContext*)1;
    D3D_FEATURE_LEVEL Feature=(D3D_FEATURE_LEVEL)0xCAFE;
    PVOID Object=(PVOID)1;
    // Poison input proves this no longer calls an unrelated native API or
    // dereferences a D3D12 device as an IDXGIAdapter.
    CHECK(D3D11On12CreateDevice((IUnknown*)1,0,NULL,0,NULL,0,0,&Device,&Context,&Feature)==DXGI_ERROR_UNSUPPORTED && !Device && !Context && Feature==(D3D_FEATURE_LEVEL)0xCAFE);
    CHECK(DCompositionCreateDevice(NULL,&IID_IUnknown,NULL)==E_INVALIDARG);
    CHECK(DCompositionCreateDevice(NULL,&IID_IUnknown,&Object)==E_NOINTERFACE && !Object);
    CHECK(CreateIDXGIFactoryMedia(&Object)==E_NOINTERFACE && !Object);
    CHECK(D3D12SerializeRootSignature(NULL,0,NULL,NULL)==E_INVALIDARG);
    CHECK(D3D12SerializeVersionedRootSignature(NULL,NULL,NULL)==E_INVALIDARG);
}
static int NativeReference(void)
{
    typedef HRESULT (WINAPI *CREATE2)(UINT,REFIID,PPVOID);
    IDXGIFactory5 *Factory=NULL;
    PVOID Object=NULL;
    BYTE Buffer[8];
    BOOL Tearing=TRUE;
    HMODULE Module=GetModuleHandleW(L"dxgi.dll");
    CREATE2 Create2=Module?(CREATE2)GetProcAddress(Module,"CreateDXGIFactory2"):NULL;
    HRESULT Result;
    UINT Index;
    const UINT Flags[]={0,1,2,0x80000000};
    if(GetModuleHandleW(L"KexDll.dll")) {puts("REFUSED|Kex is active");return 2;}
    Result=NativeFactoryForReference(&IID_IDXGIFactory5,(PPVOID)&Factory);
    printf("NATIVE_FACTORY5|hresult=0x%08lX|bits=%u\n",(ULONG)Result,(UINT)(sizeof(void*)*8));
    if(FAILED(Result) || !Factory)return 1;
    Result=Factory->lpVtbl->CheckFeatureSupport(Factory,DXGI_FEATURE_PRESENT_ALLOW_TEARING,&Tearing,sizeof(Tearing));
    printf("NATIVE_FEATURE|kind=known_bool|hresult=0x%08lX|value=%ld\n",(ULONG)Result,Tearing);
    memset(Buffer,0x5A,sizeof(Buffer));
    Result=Factory->lpVtbl->CheckFeatureSupport(Factory,(DXGI_FEATURE)999,Buffer,sizeof(Buffer));
    printf("NATIVE_FEATURE|kind=unknown|hresult=0x%08lX|first=%u|last=%u\n",(ULONG)Result,(UINT)Buffer[0],(UINT)Buffer[7]);
    Result=Factory->lpVtbl->CheckFeatureSupport(Factory,DXGI_FEATURE_PRESENT_ALLOW_TEARING,Buffer,1);
    printf("NATIVE_FEATURE|kind=size1|hresult=0x%08lX|first=%u|last=%u\n",(ULONG)Result,(UINT)Buffer[0],(UINT)Buffer[7]);
    ((IDXGIFactory2*)Factory)->lpVtbl->Release((IDXGIFactory2*)Factory);
    if(Create2)for(Index=0;Index<ARRAYSIZE(Flags);++Index) {
        Object=NULL;Result=Create2(Flags[Index],&IID_IDXGIFactory5,&Object);
        printf("NATIVE_CREATE2|flags=0x%08X|hresult=0x%08lX|object=%u\n",Flags[Index],(ULONG)Result,Object!=NULL);
        if(Object) {
            printf("NATIVE_CREATION_FLAGS|input=0x%08X|reported=0x%08lX\n",Flags[Index],
                ((IDXGIFactory3*)Object)->lpVtbl->GetCreationFlags((IDXGIFactory3*)Object));
            ((IDXGIFactory2*)Object)->lpVtbl->Release(Object);
        }
    }
    return 0;
}
static void InstalledD2DContracts(HMODULE Module)
{
    typedef HRESULT (WINAPI *CREATE_D2D)(D2D1_FACTORY_TYPE,REFIID,CONST D2D1_FACTORY_OPTIONS*,PPVOID);
    CREATE_D2D Create=(CREATE_D2D)GetProcAddress(Module,"D2D1CreateFactory");
    ID2D1Factory2 *Factory=NULL,*Second=NULL;
    ID3D11Device *D3DDevice=NULL;
    IDXGIDevice *Dxgi=NULL;
    ID2D1Device1 *FirstDevice=NULL,*SecondDevice=NULL;
    ID2D1DeviceContext1 *Context=(ID2D1DeviceContext1*)1;
    PVOID Object=NULL;
    ID2D1Factory *Owner=NULL;
    ID2D1Factory2 *SavedFactory=NULL;
    ID2D1RectangleGeometry *Geometry=(ID2D1RectangleGeometry*)1;
    D2D1_RECT_F Rectangle={0,0,1,1};
    HRESULT Result;
    CHECK(Create!=NULL);
    if(!Create)return;
    Result=Create(D2D1_FACTORY_TYPE_MULTI_THREADED,&IID_ID2D1Factory2,NULL,(PPVOID)&Factory);
    printf("INSTALLED_D2D_FACTORY2|hresult=0x%08lX\n",(ULONG)Result);
    CHECK(Result==S_OK && Factory!=NULL);
    if(FAILED(Result) || !Factory)goto Cleanup;
    CHECK(Create(D2D1_FACTORY_TYPE_MULTI_THREADED,&IID_ID2D1Factory2,NULL,(PPVOID)&Second)==S_OK && Second!=NULL);
    CHECK(Factory->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)Factory,&IID_IUnknown,&Object)==S_OK && Object==Factory);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    CHECK(Factory->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)Factory,&IID_ID2D1Factory2,&Object)==S_OK && Object==Factory);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    Result=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        NULL,0,D3D11_SDK_VERSION,&D3DDevice,NULL,NULL);
    printf("INSTALLED_D2D_HARDWARE|hresult=0x%08lX\n",(ULONG)Result);
    CHECK(Result==S_OK && D3DDevice!=NULL);
    if(FAILED(Result) || !D3DDevice)goto Cleanup;
    CHECK(D3DDevice->lpVtbl->QueryInterface(D3DDevice,&IID_IDXGIDevice,(PPVOID)&Dxgi)==S_OK && Dxgi!=NULL);
    if(!Dxgi)goto Cleanup;
    CHECK(Factory->lpVtbl->CreateDevice(Factory,Dxgi,&FirstDevice)==S_OK && FirstDevice!=NULL);
    if(!FirstDevice)goto Cleanup;
    CHECK(Factory->lpVtbl->CreateDevice(Factory,Dxgi,&SecondDevice)==S_OK && SecondDevice!=NULL && SecondDevice!=FirstDevice);
    CHECK(FirstDevice->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)FirstDevice,&IID_IUnknown,&Object)==S_OK && Object==FirstDevice);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    CHECK(FirstDevice->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)FirstDevice,&IID_ID2D1Device1,&Object)==S_OK && Object==FirstDevice);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    CHECK(FirstDevice->lpVtbl->CreateDeviceContext(FirstDevice,D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&Context)==E_NOTIMPL && !Context);
    CHECK(((ID2D1Factory*)Factory)->lpVtbl->CreateRectangleGeometry((ID2D1Factory*)Factory,&Rectangle,&Geometry)==E_NOTIMPL && !Geometry);
    FirstDevice->lpVtbl->Base.Base.GetFactory((ID2D1Resource*)FirstDevice,&Owner);
    CHECK(Owner==(ID2D1Factory*)Factory);
    if(Owner) {
        CHECK(Owner->lpVtbl->Base.QueryInterface((IUnknown*)Owner,&IID_IUnknown,&Object)==S_OK && Object==Factory);
        if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
        Owner->lpVtbl->Base.Release((IUnknown*)Owner);Owner=NULL;
    }
    // Devices, rather than this caller reference, must now retain the proxy.
    SavedFactory=Factory;Factory->lpVtbl->Base.Base.Base.Release((IUnknown*)Factory);Factory=NULL;
    FirstDevice->lpVtbl->Base.Base.GetFactory((ID2D1Resource*)FirstDevice,&Owner);
    CHECK(Owner==(ID2D1Factory*)SavedFactory);
    if(Owner) {Owner->lpVtbl->Base.Release((IUnknown*)Owner);Owner=NULL;}

Cleanup:
    if(Owner)Owner->lpVtbl->Base.Release((IUnknown*)Owner);
    if(Object)((IUnknown*)Object)->lpVtbl->Release(Object);
    if(SecondDevice)SecondDevice->lpVtbl->Base.Base.Base.Release((IUnknown*)SecondDevice);
    if(FirstDevice)FirstDevice->lpVtbl->Base.Base.Base.Release((IUnknown*)FirstDevice);
    if(Dxgi)Dxgi->lpVtbl->Release(Dxgi);
    if(D3DDevice)D3DDevice->lpVtbl->Release(D3DDevice);
    if(Second)Second->lpVtbl->Base.Base.Base.Release((IUnknown*)Second);
    if(Factory)Factory->lpVtbl->Base.Base.Base.Release((IUnknown*)Factory);
}
static int InstalledContracts(const char *Path)
{
    typedef HRESULT (WINAPI *CREATE2)(UINT,REFIID,PPVOID);
    HMODULE Module;
    CREATE2 Create2;
    IDXGIFactory7 *First=NULL,*Second=NULL;
    IDXGIFactory2 *Base;
    IDXGIAdapter1 *Adapter=NULL;
    DXGI_ADAPTER_DESC1 Desc;
    PVOID Object=NULL;
    BOOL Tearing=TRUE;
    BYTE Bytes[8];
    HRESULT Result;
    if(!GetModuleHandleW(L"KexDll.dll")) {puts("REFUSED|installed contracts require active Kex");return 2;}
    Module=LoadLibraryA(Path);
    if(!Module) {printf("LOAD_KXDX|win32=%lu\n",GetLastError());return 1;}
    Create2=(CREATE2)GetProcAddress(Module,"CreateDXGIFactory2");
    if(!Create2) {puts("FAIL|CreateDXGIFactory2 export absent");FreeLibrary(Module);return 1;}
    Result=Create2(0,&IID_IDXGIFactory7,(PPVOID)&First);
    printf("INSTALLED_FACTORY7|hresult=0x%08lX|bits=%u\n",(ULONG)Result,(UINT)(sizeof(void*)*8));
    CHECK(Result==S_OK && First!=NULL);
    if(FAILED(Result) || !First)goto Cleanup;
    CHECK(Create2(0,&IID_IDXGIFactory7,(PPVOID)&Second)==S_OK && Second!=NULL);
    Base=(IDXGIFactory2*)First;
    CHECK(Base->lpVtbl->QueryInterface(Base,&IID_IUnknown,&Object)==S_OK && Object==First);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    CHECK(Base->lpVtbl->QueryInterface(Base,&IID_IDXGIFactory7,&Object)==S_OK && Object==First);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    CHECK(((IDXGIFactory5*)First)->lpVtbl->CheckFeatureSupport((IDXGIFactory5*)First,
        DXGI_FEATURE_PRESENT_ALLOW_TEARING,&Tearing,sizeof(Tearing))==S_OK && !Tearing);
    memset(Bytes,0x5A,sizeof(Bytes));
    CHECK(((IDXGIFactory5*)First)->lpVtbl->CheckFeatureSupport((IDXGIFactory5*)First,
        (DXGI_FEATURE)999,Bytes,sizeof(Bytes))==DXGI_ERROR_INVALID_CALL && Bytes[0]==0x5A && Bytes[7]==0x5A);
    CHECK(((IDXGIFactory5*)First)->lpVtbl->CheckFeatureSupport((IDXGIFactory5*)First,
        DXGI_FEATURE_PRESENT_ALLOW_TEARING,Bytes,1)==DXGI_ERROR_INVALID_CALL && Bytes[0]==0x5A);
    CHECK(Base->lpVtbl->EnumAdapters1(Base,0,&Adapter)==S_OK && Adapter!=NULL);
    if(!Adapter)goto Cleanup;
    Result=Adapter->lpVtbl->GetDesc1(Adapter,&Desc);
    CHECK(Result==S_OK);
    if(FAILED(Result))goto Cleanup;
    Result=((IDXGIFactory4*)First)->lpVtbl->EnumAdapterByLuid((IDXGIFactory4*)First,
        Desc.AdapterLuid,&IID_IDXGIAdapter4,&Object);
    CHECK(Result==S_OK && Object!=NULL);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    // Enumerating the same native adapter a second time must not recurse.
    Result=((IDXGIFactory6*)First)->lpVtbl->EnumAdapterByGpuPreference((IDXGIFactory6*)First,
        0,DXGI_GPU_PREFERENCE_UNSPECIFIED,&IID_IDXGIAdapter4,&Object);
    CHECK(Result==S_OK && Object!=NULL);
    if(Object) {((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;}
    Result=((IDXGIFactory6*)First)->lpVtbl->EnumAdapterByGpuPreference((IDXGIFactory6*)First,
        0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,&IID_IDXGIAdapter1,&Object);
    CHECK(Result==DXGI_ERROR_UNSUPPORTED && Object==NULL);
    InstalledD2DContracts(Module);
Cleanup:
    if(Object)((IUnknown*)Object)->lpVtbl->Release(Object);
    if(Adapter)Adapter->lpVtbl->Release(Adapter);
    if(Second)((IDXGIFactory2*)Second)->lpVtbl->Release((IDXGIFactory2*)Second);
    if(First)((IDXGIFactory2*)First)->lpVtbl->Release((IDXGIFactory2*)First);
    FreeLibrary(Module);
    printf("%s|installed_kxdx_contracts|assertions=%u|failures=%u\n",RuntimeFailures?"FAIL":"PASS",Assertions,RuntimeFailures);
    return RuntimeFailures?1:0;
}
int main(int Count,char **Arguments)
{
    int Result;
    setvbuf(stdout,NULL,_IONBF,0);
    if(Count==2 && !strcmp(Arguments[1],"--native-reference"))return NativeReference();
    if(Count==3 && !strcmp(Arguments[1],"--installed"))return InstalledContracts(Arguments[2]);
    if(Count!=1)return 2;
    Result=D2DProxyMockMain();if(Result)return Result;
    CheckUnsupportedBackends();
    printf("%s|assertions=%u|failures=%u|live_wrapper_tables=%ld|scope=d2d_outer_and_backend_contracts\n",RuntimeFailures?"FAIL":"PASS",Checks+Assertions,RuntimeFailures,Live);
    return RuntimeFailures?1:0;
}
