// Independent outer-COM lifetime/ABI checks; never loads a target DLL.
#include "../../01-Extended DLLs/KxDx/buildcfg.h"
#include "../../01-Extended DLLs/KxDx/kxdxp.h"
#include <KexW32ML.h>
#include <stdio.h>
static LONG Live,Allocated,Freed;
static BOOL FailAllocation;
static PVOID TestAllocate(HANDLE Heap,DWORD Flags,SIZE_T Bytes)
{PVOID P;if(FailAllocation)return NULL;P=HeapAlloc(Heap,Flags,Bytes);if(P){InterlockedIncrement(&Live);InterlockedIncrement(&Allocated);}return P;}
static BOOL TestFree(HANDLE Heap,DWORD Flags,PVOID P)
{BOOL R=HeapFree(Heap,Flags,P);if(R){InterlockedDecrement(&Live);InterlockedIncrement(&Freed);}return R;}
#undef KexLogEvent
#define KexLogEvent(...) ((void)0)
#undef HeapAlloc
#undef HeapFree
#define HeapAlloc TestAllocate
#define HeapFree TestFree
#include "../../01-Extended DLLs/KxDx/direct2d.c"
#include "../../01-Extended DLLs/KxDx/iid.c"

static UINT Checks,Failures,FactoryTeardowns,DeviceTeardowns,NativeCreateIndex,NativeGeometryCalls;
#define CHECK(E) do{++Checks;if(!(E)){++Failures;printf("FAIL|line=%u|%s\n",__LINE__,#E);}}while(0)
typedef struct {KX_NATIVE_FACTORY1_VTBL Public;VOID (STDMETHODCALLTYPE *PrivateTeardown)(IUnknown*);} NATIVE_FACTORY_TABLE;
typedef struct {KX_NATIVE_DEVICE_VTBL Public;VOID (STDMETHODCALLTYPE *PrivateTeardown)(IUnknown*);} NATIVE_DEVICE_TABLE;
typedef struct {ID2D1Factory1 Interface;LONG References;} NATIVE_FACTORY;
typedef struct {ID2D1Device Interface;LONG References;NATIVE_FACTORY *Factory;UINT64 Memory;UINT32 Cleared;} NATIVE_DEVICE;
static NATIVE_FACTORY_TABLE FactoryTable;
static NATIVE_DEVICE_TABLE DeviceTable;
static NATIVE_FACTORY Factories[2];
static NATIVE_DEVICE Devices[8];
static BOOL FailNativeCreate;
static ULONG STDMETHODCALLTYPE NativeFactoryAddRef(IUnknown *This)
{return (ULONG)InterlockedIncrement(&((NATIVE_FACTORY*)This)->References);}
static VOID STDMETHODCALLTYPE NativeFactoryTeardown(IUnknown *This)
{CHECK(This->lpVtbl==(IUnknownVtbl*)&FactoryTable.Public);++FactoryTeardowns;}
static ULONG STDMETHODCALLTYPE NativeFactoryRelease(IUnknown *This)
{
    LONG R=InterlockedDecrement(&((NATIVE_FACTORY*)This)->References);
    if(!R)((NATIVE_FACTORY_TABLE*)This->lpVtbl)->PrivateTeardown(This);
    return (ULONG)R;
}
static HRESULT STDMETHODCALLTYPE NativeFactoryQI(IUnknown *This,REFIID Iid,PPVOID Object)
{
    *Object=NULL;
    if(IsEqualIID(Iid,&IID_IUnknown)||IsEqualIID(Iid,&IID_ID2D1Factory)||IsEqualIID(Iid,&IID_ID2D1Factory1)) {
        *Object=This;NativeFactoryAddRef(This);return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE NativeDeviceAddRef(IUnknown *This)
{return (ULONG)InterlockedIncrement(&((NATIVE_DEVICE*)This)->References);}
static VOID STDMETHODCALLTYPE NativeDeviceTeardown(IUnknown *This)
{
    CHECK(This->lpVtbl==(IUnknownVtbl*)&DeviceTable.Public);++DeviceTeardowns;
    NativeFactoryRelease((IUnknown*)((NATIVE_DEVICE*)This)->Factory);
}
static ULONG STDMETHODCALLTYPE NativeDeviceRelease(IUnknown *This)
{
    LONG R=InterlockedDecrement(&((NATIVE_DEVICE*)This)->References);
    if(!R)((NATIVE_DEVICE_TABLE*)This->lpVtbl)->PrivateTeardown(This);
    return (ULONG)R;
}
static HRESULT STDMETHODCALLTYPE NativeDeviceQI(IUnknown *This,REFIID Iid,PPVOID Object)
{*Object=NULL;if(IsEqualIID(Iid,&IID_IUnknown)){*Object=This;NativeDeviceAddRef(This);return S_OK;}return E_NOINTERFACE;}
static VOID STDMETHODCALLTYPE NativeGetFactory(ID2D1Resource *This,ID2D1Factory **Factory)
{*Factory=(ID2D1Factory*)((NATIVE_DEVICE*)This)->Factory;NativeFactoryAddRef((IUnknown*)*Factory);}
static HRESULT STDMETHODCALLTYPE NativeCreate(ID2D1Factory1 *This,IDXGIDevice *Dxgi,ID2D1Device **Device)
{
    NATIVE_DEVICE *Next;
    UNREFERENCED_PARAMETER(Dxgi);*Device=NULL;if(FailNativeCreate)return E_FAIL;
    if(NativeCreateIndex>=ARRAYSIZE(Devices))return E_OUTOFMEMORY;
    Next=&Devices[NativeCreateIndex++];Next->References=1;Next->Factory=(NATIVE_FACTORY*)This;
    NativeFactoryAddRef((IUnknown*)This);*Device=&Next->Interface;return S_OK;
}
static HRESULT STDMETHODCALLTYPE NativeRectangle(ID2D1Factory *This,CONST D2D1_RECT_F *Rect,ID2D1RectangleGeometry **Geometry)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Rect);++NativeGeometryCalls;*Geometry=NULL;return E_FAIL;}
static HRESULT STDMETHODCALLTYPE NativeReload(ID2D1Factory *This)
{CHECK(This==(ID2D1Factory*)&Factories[0]);return S_FALSE;}
static VOID STDMETHODCALLTYPE NativeDpi(ID2D1Factory *This,FLOAT *X,FLOAT *Y)
{CHECK(This==(ID2D1Factory*)&Factories[0]);*X=96;*Y=120;}
static VOID STDMETHODCALLTYPE NativeSetMemory(ID2D1Device *This,UINT64 Bytes)
{((NATIVE_DEVICE*)This)->Memory=Bytes;}
static UINT64 STDMETHODCALLTYPE NativeGetMemory(ID2D1Device *This)
{return ((NATIVE_DEVICE*)This)->Memory;}
static VOID STDMETHODCALLTYPE NativeClear(ID2D1Device *This,UINT32 Milliseconds)
{((NATIVE_DEVICE*)This)->Cleared=Milliseconds;}
static DWORD WINAPI RefWorker(PVOID Parameter)
{
    IUnknown *Object=(IUnknown*)Parameter;UINT I;
    for(I=0;I<10000;++I){Object->lpVtbl->AddRef(Object);Object->lpVtbl->Release(Object);}return 0;
}
int main(void)
{
    ID2D1Factory1 *First,*Second,*Before;
    ID2D1Device1 *D0=NULL,*D1=NULL;
    ID2D1Factory *FromDevice=NULL;
    ID2D1RectangleGeometry *Rectangle=(ID2D1RectangleGeometry*)1;
    ID2D1DeviceContext1 *Context=(ID2D1DeviceContext1*)1;
    ID2D1DeviceContext *BaseContext=(ID2D1DeviceContext*)1;
    D2D1_RECT_F Bounds={0,0,1,1};
    PVOID Object=(PVOID)1;
    CONST GUID Unknown={0x01234567,0x1234,0x1234,{1,2,3,4,5,6,7,8}};
    HANDLE Threads[4];UINT I;DWORD ExitCode;FLOAT X=0,Y=0;
    setvbuf(stdout,NULL,_IONBF,0);
    FactoryTable.Public.Base.Base.QueryInterface=NativeFactoryQI;
    FactoryTable.Public.Base.Base.AddRef=NativeFactoryAddRef;
    FactoryTable.Public.Base.Base.Release=NativeFactoryRelease;
    FactoryTable.Public.Base.ReloadSystemMetrics=NativeReload;
    FactoryTable.Public.Base.GetDesktopDpi=NativeDpi;
    FactoryTable.Public.Base.CreateRectangleGeometry=NativeRectangle;
    FactoryTable.Public.CreateDevice=NativeCreate;FactoryTable.PrivateTeardown=NativeFactoryTeardown;
    DeviceTable.Public.Base.Base.QueryInterface=NativeDeviceQI;
    DeviceTable.Public.Base.Base.AddRef=NativeDeviceAddRef;
    DeviceTable.Public.Base.Base.Release=NativeDeviceRelease;
    DeviceTable.Public.Base.GetFactory=NativeGetFactory;
    DeviceTable.Public.SetMaximumTextureMemory=NativeSetMemory;
    DeviceTable.Public.GetMaximumTextureMemory=NativeGetMemory;
    DeviceTable.Public.ClearResources=NativeClear;DeviceTable.PrivateTeardown=NativeDeviceTeardown;
    for(I=0;I<ARRAYSIZE(Factories);++I){Factories[I].Interface.lpVtbl=(ID2D1Factory1Vtbl*)&FactoryTable.Public;Factories[I].References=1;}
    for(I=0;I<ARRAYSIZE(Devices);++I)Devices[I].Interface.lpVtbl=(ID2D1DeviceVtbl*)&DeviceTable.Public;
    First=&Factories[0].Interface;Second=&Factories[1].Interface;
    CHECK(WrapDirect2DFactory(NULL)==E_INVALIDARG);
    Before=First;FailAllocation=TRUE;
    CHECK(WrapDirect2DFactory(&First)==E_OUTOFMEMORY && First==Before && Factories[0].References==1);
    FailAllocation=FALSE;
    CHECK(WrapDirect2DFactory(&First)==S_OK && First!=Before);
    Before=First;CHECK(WrapDirect2DFactory(&First)==S_OK && First==Before && Live==1);
    CHECK(WrapDirect2DFactory(&Second)==S_OK && Second!=&Factories[1].Interface);
    CHECK(Factories[0].Interface.lpVtbl==(ID2D1Factory1Vtbl*)&FactoryTable.Public);
    CHECK(Factories[1].Interface.lpVtbl==(ID2D1Factory1Vtbl*)&FactoryTable.Public);
    CHECK(First->lpVtbl->Base.Base.QueryInterface((IUnknown*)First,NULL,&Object)==E_INVALIDARG && !Object);
    CHECK(First->lpVtbl->Base.Base.QueryInterface((IUnknown*)First,&IID_IUnknown,NULL)==E_POINTER);
    CHECK(First->lpVtbl->Base.Base.QueryInterface((IUnknown*)First,&Unknown,&Object)==E_NOINTERFACE && !Object);
    CHECK(First->lpVtbl->Base.Base.QueryInterface((IUnknown*)First,&IID_IUnknown,&Object)==S_OK && Object==First);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(First->lpVtbl->Base.Base.QueryInterface((IUnknown*)First,&IID_ID2D1Factory,&Object)==S_OK && Object==First);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(First->lpVtbl->Base.Base.QueryInterface((IUnknown*)First,&IID_ID2D1Factory1,&Object)==S_OK && Object==First);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(First->lpVtbl->Base.Base.QueryInterface((IUnknown*)First,&IID_ID2D1Factory2,&Object)==S_OK && Object==First);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(First->lpVtbl->Base.ReloadSystemMetrics((ID2D1Factory*)First)==S_FALSE);
    First->lpVtbl->Base.GetDesktopDpi((ID2D1Factory*)First,&X,&Y);CHECK(X==96 && Y==120);
    CHECK(First->lpVtbl->Base.CreateRectangleGeometry((ID2D1Factory*)First,&Bounds,&Rectangle)==E_NOTIMPL && !Rectangle && !NativeGeometryCalls);
    FailAllocation=TRUE;
    CHECK(ID2D1Factory2_CreateDevice((ID2D1Factory2*)First,(IDXGIDevice*)1,&D0)==E_OUTOFMEMORY && !D0 && Factories[0].References==1 && DeviceTeardowns==1);
    FailAllocation=FALSE;FailNativeCreate=TRUE;
    CHECK(ID2D1Factory2_CreateDevice((ID2D1Factory2*)First,(IDXGIDevice*)1,&D0)==E_FAIL && !D0);
    FailNativeCreate=FALSE;
    CHECK(ID2D1Factory2_CreateDevice((ID2D1Factory2*)First,NULL,&D0)==E_INVALIDARG && !D0);
    CHECK(ID2D1Factory2_CreateDevice((ID2D1Factory2*)First,(IDXGIDevice*)1,&D0)==S_OK && D0);
    CHECK(ID2D1Factory2_CreateDevice((ID2D1Factory2*)First,(IDXGIDevice*)1,&D1)==S_OK && D1 && D1!=D0);
    CHECK(Devices[1].Interface.lpVtbl==(ID2D1DeviceVtbl*)&DeviceTable.Public && Devices[2].Interface.lpVtbl==(ID2D1DeviceVtbl*)&DeviceTable.Public);
    Before=(ID2D1Factory1*)D0;
    CHECK(WrapDirect2DDevice((ID2D1Device**)&D0,(ID2D1Factory2*)First)==S_OK && D0==(ID2D1Device1*)Before);
    CHECK(D0->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)D0,&IID_IUnknown,&Object)==S_OK && Object==D0);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(D0->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)D0,&IID_ID2D1Resource,&Object)==S_OK && Object==D0);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(D0->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)D0,&KxIID_ID2D1Device,&Object)==S_OK && Object==D0);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(D0->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)D0,&IID_ID2D1Device1,&Object)==S_OK && Object==D0);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    CHECK(D0->lpVtbl->Base.Base.Base.QueryInterface((IUnknown*)D0,&Unknown,&Object)==E_NOINTERFACE && !Object);
    D0->lpVtbl->Base.Base.GetFactory((ID2D1Resource*)D0,&FromDevice);
    CHECK(FromDevice==(ID2D1Factory*)First);
    CHECK(FromDevice->lpVtbl->Base.QueryInterface((IUnknown*)FromDevice,&IID_IUnknown,&Object)==S_OK && Object==First);((IUnknown*)Object)->lpVtbl->Release(Object);Object=NULL;
    FromDevice->lpVtbl->Base.Release((IUnknown*)FromDevice);FromDevice=NULL;
    CHECK(D0->lpVtbl->CreateDeviceContext(D0,D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&Context)==E_NOTIMPL && !Context);
    CHECK(D0->lpVtbl->CreateDeviceContext(D0,(D2D1_DEVICE_CONTEXT_OPTIONS)2,&Context)==E_INVALIDARG && !Context);
    CHECK(((KX_DEVICE1_VTBL*)D0->lpVtbl)->Base.CreateDeviceContext((ID2D1Device*)D0,D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&BaseContext)==E_NOTIMPL && !BaseContext);
    ((KX_DEVICE1_VTBL*)D0->lpVtbl)->Base.SetMaximumTextureMemory((ID2D1Device*)D0,0x1234567812345678ui64);
    CHECK(((KX_DEVICE1_VTBL*)D0->lpVtbl)->Base.GetMaximumTextureMemory((ID2D1Device*)D0)==0x1234567812345678ui64);
    ((KX_DEVICE1_VTBL*)D0->lpVtbl)->Base.ClearResources((ID2D1Device*)D0,1234567);CHECK(Devices[1].Cleared==1234567);
    for(I=0;I<ARRAYSIZE(Threads);++I){Threads[I]=CreateThread(NULL,0,RefWorker,D0,0,NULL);if(!Threads[I])ExitProcess(88);}
    CHECK(WaitForMultipleObjects(ARRAYSIZE(Threads),Threads,TRUE,10000)==WAIT_OBJECT_0);
    for(I=0;I<ARRAYSIZE(Threads);++I){CHECK(GetExitCodeThread(Threads[I],&ExitCode) && ExitCode==0);CloseHandle(Threads[I]);}
    CHECK(((KX_D2D_DEVICE*)D0)->References==1 && Devices[1].References==1);
    CHECK(Second->lpVtbl->Base.Base.Release((IUnknown*)Second)==0 && FactoryTeardowns==1);
    CHECK(First->lpVtbl->Base.Base.Release((IUnknown*)First)==2);
    D0->lpVtbl->Base.Base.GetFactory((ID2D1Resource*)D0,&FromDevice);CHECK(FromDevice==(ID2D1Factory*)First);FromDevice->lpVtbl->Base.Release((IUnknown*)FromDevice);
    CHECK(D0->lpVtbl->Base.Base.Base.Release((IUnknown*)D0)==0);
    CHECK(D1->lpVtbl->Base.Base.Base.Release((IUnknown*)D1)==0);
    CHECK(FactoryTeardowns==2 && DeviceTeardowns==3);
    CHECK(Factories[0].References==0 && Factories[1].References==0);
    CHECK(Live==0 && Allocated==Freed);
    printf("%s|d2d_outer_proxy|checks=%u|failures=%u|live=%ld|allocated=%ld|freed=%ld|native_private_factory_teardown=%u|native_private_device_teardown=%u\n",Failures?"FAIL":"PASS",Checks,Failures,Live,Allocated,Freed,FactoryTeardowns,DeviceTeardowns);
    return Failures?1:0;
}
