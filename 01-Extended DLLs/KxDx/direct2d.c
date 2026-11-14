#include "buildcfg.h"
#include "kxdxp.h"
#include <KexW32ML.h>
#include <d2d1.h>

// A COM implementation may have private virtual slots after its public vtable.
// Never replace that table: native teardown can call those private methods.
// These outer objects own a native reference and publish only their own ABI.
// This Factory2/Device1 safe subset does not provide resource/context proxies.
// Those outputs return E_NOTIMPL/null to preserve identity; ordinary native
// Factory0/Factory1 requests continue through factory.c without this wrapper.
typedef struct ID2D1StrokeStyle1 ID2D1StrokeStyle1;
typedef struct ID2D1PathGeometry1 ID2D1PathGeometry1;
typedef struct ID2D1DrawingStateBlock1 ID2D1DrawingStateBlock1;
typedef struct ID2D1GdiMetafile ID2D1GdiMetafile;
typedef struct ID2D1Properties ID2D1Properties;
typedef struct ID2D1DeviceContext ID2D1DeviceContext;
typedef struct ID2D1PrintControl ID2D1PrintControl;
typedef struct IPrintDocumentPackageTarget IPrintDocumentPackageTarget;
typedef struct IWICImagingFactory IWICImagingFactory;
typedef struct KX_STROKE_STYLE_PROPERTIES1 KX_STROKE_STYLE_PROPERTIES1;
typedef struct KX_DRAWING_STATE_DESCRIPTION1 KX_DRAWING_STATE_DESCRIPTION1;
typedef struct KX_PROPERTY_BINDING KX_PROPERTY_BINDING;
typedef struct KX_PRINT_CONTROL_PROPERTIES KX_PRINT_CONTROL_PROPERTIES;
typedef HRESULT (CALLBACK *KX_EFFECT_FACTORY)(IUnknown **Effect);

// Exact pointer/scalar ABI of the public native Factory1/Device methods in
// Microsoft's d2d1_1.h; opaque pointee types are forwarded without inspection.
typedef struct {
    ID2D1FactoryVtbl Base;
    HRESULT (STDMETHODCALLTYPE *CreateDevice)(ID2D1Factory1 *This,IDXGIDevice *Dxgi,ID2D1Device **Device);
    HRESULT (STDMETHODCALLTYPE *CreateStrokeStyle1)(ID2D1Factory1 *This, CONST KX_STROKE_STYLE_PROPERTIES1 * Properties, CONST FLOAT * Dashes, UINT32 Count, ID2D1StrokeStyle1 ** Style);
    HRESULT (STDMETHODCALLTYPE *CreatePathGeometry1)(ID2D1Factory1 *This, ID2D1PathGeometry1 ** Geometry);
    HRESULT (STDMETHODCALLTYPE *CreateDrawingStateBlock1)(ID2D1Factory1 *This, CONST KX_DRAWING_STATE_DESCRIPTION1 * Description, IDWriteRenderingParams * Text, ID2D1DrawingStateBlock1 ** Block);
    HRESULT (STDMETHODCALLTYPE *CreateGdiMetafile)(ID2D1Factory1 *This, IStream * Stream, ID2D1GdiMetafile ** Metafile);
    HRESULT (STDMETHODCALLTYPE *RegisterEffectFromStream)(ID2D1Factory1 *This, REFCLSID ClassId, IStream * Xml, CONST KX_PROPERTY_BINDING * Bindings, UINT32 Count, KX_EFFECT_FACTORY Factory);
    HRESULT (STDMETHODCALLTYPE *RegisterEffectFromString)(ID2D1Factory1 *This, REFCLSID ClassId, PCWSTR Xml, CONST KX_PROPERTY_BINDING * Bindings, UINT32 Count, KX_EFFECT_FACTORY Factory);
    HRESULT (STDMETHODCALLTYPE *UnregisterEffect)(ID2D1Factory1 *This, REFCLSID ClassId);
    HRESULT (STDMETHODCALLTYPE *GetRegisteredEffects)(ID2D1Factory1 *This, CLSID * Effects, UINT32 Count, UINT32 * Returned, UINT32 * Registered);
    HRESULT (STDMETHODCALLTYPE *GetEffectProperties)(ID2D1Factory1 *This, REFCLSID EffectId, ID2D1Properties ** Properties);
} KX_NATIVE_FACTORY1_VTBL;

typedef struct {
    ID2D1ResourceVtbl Base;
    HRESULT (STDMETHODCALLTYPE *CreateDeviceContext)(ID2D1Device *This, D2D1_DEVICE_CONTEXT_OPTIONS Options, ID2D1DeviceContext ** Context);
    HRESULT (STDMETHODCALLTYPE *CreatePrintControl)(ID2D1Device *This, IWICImagingFactory * WicFactory, IPrintDocumentPackageTarget * Document, CONST KX_PRINT_CONTROL_PROPERTIES * Properties, ID2D1PrintControl ** Control);
    VOID (STDMETHODCALLTYPE *SetMaximumTextureMemory)(ID2D1Device *This, UINT64 Bytes);
    UINT64 (STDMETHODCALLTYPE *GetMaximumTextureMemory)(ID2D1Device *This);
    VOID (STDMETHODCALLTYPE *ClearResources)(ID2D1Device *This, UINT32 Milliseconds);
} KX_NATIVE_DEVICE_VTBL;

typedef struct {
    KX_NATIVE_FACTORY1_VTBL Base;
    HRESULT (STDMETHODCALLTYPE *CreateDevice1)(ID2D1Factory2*,IDXGIDevice*,ID2D1Device1**);
} KX_FACTORY2_VTBL;

typedef struct {
    KX_NATIVE_DEVICE_VTBL Base;
    D2D1_RENDERING_PRIORITY (STDMETHODCALLTYPE *GetRenderingPriority)(ID2D1Device1*);
    VOID (STDMETHODCALLTYPE *SetRenderingPriority)(ID2D1Device1*,D2D1_RENDERING_PRIORITY);
    HRESULT (STDMETHODCALLTYPE *CreateDeviceContext1)(ID2D1Device1*,D2D1_DEVICE_CONTEXT_OPTIONS,ID2D1DeviceContext1**);
} KX_DEVICE1_VTBL;

typedef struct {
    CONST KX_FACTORY2_VTBL *Vtbl;
    LONG References;
    ID2D1Factory1 *Native;
} KX_D2D_FACTORY;
typedef struct {
    CONST KX_DEVICE1_VTBL *Vtbl;
    LONG References;
    ID2D1Device *Native;
    KX_D2D_FACTORY *Owner;
} KX_D2D_DEVICE;

static CONST GUID KxIID_ID2D1Device = {0x47dd575d,0xac05,0x4cdd,{0x80,0x49,0x9b,0x02,0xcd,0x16,0xf4,0x4c}};
static CONST KX_FACTORY2_VTBL KxD2DFactoryVtbl;
static CONST KX_DEVICE1_VTBL KxD2DDeviceVtbl;
C_ASSERT(sizeof(KX_NATIVE_FACTORY1_VTBL)==27*sizeof(PVOID));
C_ASSERT(sizeof(KX_FACTORY2_VTBL)==28*sizeof(PVOID));
C_ASSERT(sizeof(KX_NATIVE_DEVICE_VTBL)==9*sizeof(PVOID));
C_ASSERT(sizeof(KX_DEVICE1_VTBL)==12*sizeof(PVOID));

static ULONG STDMETHODCALLTYPE KxD2DFactory_AddRef(IUnknown *This)
{return (ULONG)InterlockedIncrement(&((KX_D2D_FACTORY*)This)->References);}
static ULONG STDMETHODCALLTYPE KxD2DFactory_Release(IUnknown *This)
{
    KX_D2D_FACTORY *Proxy=(KX_D2D_FACTORY*)This;
    LONG References=InterlockedDecrement(&Proxy->References);
    if(!References) {
        ID2D1Factory1 *Native=Proxy->Native;
        // The native object still has its untouched original vtable here.
        Native->lpVtbl->Base.Base.Release((IUnknown*)Native);
        HeapFree(GetProcessHeap(),0,Proxy);
    }
    return (ULONG)References;
}
HRESULT STDMETHODCALLTYPE ID2D1Factory2_QueryInterface(ID2D1Factory2 *This,REFIID Iid,PPVOID Object)
{
    if(!Object)return E_POINTER;
    *Object=NULL;if(!Iid)return E_INVALIDARG;
    if(IsEqualIID(Iid,&IID_IUnknown) || IsEqualIID(Iid,&IID_ID2D1Factory) ||
       IsEqualIID(Iid,&IID_ID2D1Factory1) || IsEqualIID(Iid,&IID_ID2D1Factory2)) {
        *Object=This;KxD2DFactory_AddRef((IUnknown*)This);return S_OK;
    }
    // Never leak a native QI result with a different controlling IUnknown.
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE KxD2DFactory_QI(IUnknown *This,REFIID Iid,PPVOID Object)
{return ID2D1Factory2_QueryInterface((ID2D1Factory2*)This,Iid,Object);}

static ULONG STDMETHODCALLTYPE KxD2DDevice_AddRef(IUnknown *This)
{return (ULONG)InterlockedIncrement(&((KX_D2D_DEVICE*)This)->References);}
static ULONG STDMETHODCALLTYPE KxD2DDevice_Release(IUnknown *This)
{
    KX_D2D_DEVICE *Proxy=(KX_D2D_DEVICE*)This;
    LONG References=InterlockedDecrement(&Proxy->References);
    if(!References) {
        ID2D1Device *Native=Proxy->Native;
        KX_D2D_FACTORY *Owner=Proxy->Owner;
        Native->lpVtbl->Base.Base.Release((IUnknown*)Native);
        HeapFree(GetProcessHeap(),0,Proxy);
        if(Owner)KxD2DFactory_Release((IUnknown*)Owner);
    }
    return (ULONG)References;
}
HRESULT STDMETHODCALLTYPE ID2D1Device1_QueryInterface(ID2D1Device1 *This,REFIID Iid,PPVOID Object)
{
    if(!Object)return E_POINTER;
    *Object=NULL;if(!Iid)return E_INVALIDARG;
    if(IsEqualIID(Iid,&IID_IUnknown) || IsEqualIID(Iid,&IID_ID2D1Resource) ||
       IsEqualIID(Iid,&KxIID_ID2D1Device) || IsEqualIID(Iid,&IID_ID2D1Device1)) {
        *Object=This;KxD2DDevice_AddRef((IUnknown*)This);return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE KxD2DDevice_QI(IUnknown *This,REFIID Iid,PPVOID Object)
{return ID2D1Device1_QueryInterface((ID2D1Device1*)This,Iid,Object);}
static VOID STDMETHODCALLTYPE KxD2DDevice_GetFactory(ID2D1Resource *This,ID2D1Factory **Factory)
{
    KX_D2D_DEVICE *Proxy=(KX_D2D_DEVICE*)This;
    if(!Factory)return;
    *Factory=NULL;
    if(Proxy->Owner) {
        *Factory=(ID2D1Factory*)Proxy->Owner;KxD2DFactory_AddRef((IUnknown*)Proxy->Owner);
    }
}

HRESULT WrapDirect2DFactory(ID2D1Factory1 **Factory)
{
    KX_D2D_FACTORY *Proxy;
    if(!Factory || !*Factory)return E_INVALIDARG;
    if((PVOID)(*Factory)->lpVtbl==(PVOID)&KxD2DFactoryVtbl)return S_OK;
    Proxy=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*Proxy));
    if(!Proxy)return E_OUTOFMEMORY;
    Proxy->Vtbl=&KxD2DFactoryVtbl;Proxy->References=1;Proxy->Native=*Factory;
    *Factory=(ID2D1Factory1*)Proxy;return S_OK;
}
HRESULT WrapDirect2DDevice(ID2D1Device **Device,ID2D1Factory2 *Owner)
{
    KX_D2D_DEVICE *Proxy;
    if(!Device || !*Device || !Owner ||
       (PVOID)Owner->lpVtbl!=(PVOID)&KxD2DFactoryVtbl)return E_INVALIDARG;
    if((PVOID)(*Device)->lpVtbl==(PVOID)&KxD2DDeviceVtbl)return S_OK;
    Proxy=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*Proxy));
    if(!Proxy)return E_OUTOFMEMORY;
    Proxy->Vtbl=&KxD2DDeviceVtbl;Proxy->References=1;Proxy->Native=*Device;
    Proxy->Owner=(KX_D2D_FACTORY*)Owner;KxD2DFactory_AddRef((IUnknown*)Owner);
    *Device=(ID2D1Device*)Proxy;return S_OK;
}
HRESULT STDMETHODCALLTYPE ID2D1Factory2_CreateDevice(ID2D1Factory2 *This,IDXGIDevice *Dxgi,ID2D1Device1 **Device)
{
    KX_D2D_FACTORY *Proxy=(KX_D2D_FACTORY*)This;
    KX_NATIVE_FACTORY1_VTBL *Table;
    ID2D1Device *Native=NULL;
    HRESULT Result;
    if(!Device)return E_INVALIDARG;
    *Device=NULL;if(!Dxgi)return E_INVALIDARG;
    Table=(KX_NATIVE_FACTORY1_VTBL*)Proxy->Native->lpVtbl;
    Result=Table->CreateDevice(Proxy->Native,Dxgi,&Native);
    if(FAILED(Result))return Result;
    if(!Native)return E_UNEXPECTED;
    Result=WrapDirect2DDevice(&Native,This);
    if(FAILED(Result)) {Native->lpVtbl->Base.Base.Release((IUnknown*)Native);return Result;}
    *Device=(ID2D1Device1*)Native;return S_OK;
}
static HRESULT STDMETHODCALLTYPE KxD2DFactory_CreateDevice(ID2D1Factory1 *This,IDXGIDevice *Dxgi,ID2D1Device **Device)
{return ID2D1Factory2_CreateDevice((ID2D1Factory2*)This,Dxgi,(ID2D1Device1**)Device);}

// These pre-existing priority limitations remain open. No scheduling effect is
// claimed. A Context1 backend is not supplied by a base native device context.
D2D1_RENDERING_PRIORITY STDMETHODCALLTYPE ID2D1Device1_GetRenderingPriority(ID2D1Device1 *This)
{UNREFERENCED_PARAMETER(This);return D2D1_RENDERING_PRIORITY_NORMAL;}
VOID STDMETHODCALLTYPE ID2D1Device1_SetRenderingPriority(ID2D1Device1 *This,D2D1_RENDERING_PRIORITY Priority)
{UNREFERENCED_PARAMETER(This);UNREFERENCED_PARAMETER(Priority);}
HRESULT STDMETHODCALLTYPE ID2D1Device1_CreateDeviceContext(ID2D1Device1 *This,D2D1_DEVICE_CONTEXT_OPTIONS Options,ID2D1DeviceContext1 **Context)
{
    UNREFERENCED_PARAMETER(This);
    if(!Context)return E_INVALIDARG;
    *Context=NULL;
    if(Options & ~D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS)return E_INVALIDARG;
    KexLogWarningEvent(L"ID2D1DeviceContext1 backend is unavailable");return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_ReloadSystemMetrics(ID2D1Factory *This)
{
    ID2D1Factory *Native=(ID2D1Factory*)((KX_D2D_FACTORY*)This)->Native;
    return Native->lpVtbl->ReloadSystemMetrics(Native);
}

static VOID STDMETHODCALLTYPE KxFactory_GetDesktopDpi(ID2D1Factory *This, FLOAT * DpiX, FLOAT * DpiY)
{
    ID2D1Factory *Native=(ID2D1Factory*)((KX_D2D_FACTORY*)This)->Native;
    Native->lpVtbl->GetDesktopDpi(Native, DpiX, DpiY);
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateRectangleGeometry(ID2D1Factory *This, CONST D2D1_RECT_F * Rectangle, ID2D1RectangleGeometry ** Geometry)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Rectangle);
    if(!Geometry)return E_INVALIDARG;
    *Geometry=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateRoundedRectangleGeometry(ID2D1Factory *This, CONST D2D1_ROUNDED_RECT * Rectangle, ID2D1RoundedRectangleGeometry ** Geometry)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Rectangle);
    if(!Geometry)return E_INVALIDARG;
    *Geometry=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateEllipseGeometry(ID2D1Factory *This, CONST D2D1_ELLIPSE * Ellipse, ID2D1EllipseGeometry ** Geometry)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Ellipse);
    if(!Geometry)return E_INVALIDARG;
    *Geometry=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateGeometryGroup(ID2D1Factory *This, D2D1_FILL_MODE Mode, ID2D1Geometry ** Geometries, UINT Count, ID2D1GeometryGroup ** Group)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Mode);
    UNREFERENCED_PARAMETER(Geometries);
    UNREFERENCED_PARAMETER(Count);
    if(!Group)return E_INVALIDARG;
    *Group=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateTransformedGeometry(ID2D1Factory *This, ID2D1Geometry * Source, CONST D2D1_MATRIX_3X2_F * Transform, ID2D1TransformedGeometry ** Geometry)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(Transform);
    if(!Geometry)return E_INVALIDARG;
    *Geometry=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreatePathGeometry(ID2D1Factory *This, ID2D1PathGeometry ** Geometry)
{
    UNREFERENCED_PARAMETER(This);
    if(!Geometry)return E_INVALIDARG;
    *Geometry=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateStrokeStyle(ID2D1Factory *This, CONST D2D1_STROKE_STYLE_PROPERTIES * Properties, CONST FLOAT * Dashes, UINT Count, ID2D1StrokeStyle ** Style)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Properties);
    UNREFERENCED_PARAMETER(Dashes);
    UNREFERENCED_PARAMETER(Count);
    if(!Style)return E_INVALIDARG;
    *Style=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateDrawingStateBlock(ID2D1Factory *This, CONST D2D1_DRAWING_STATE_DESCRIPTION * Description, IDWriteRenderingParams * Text, ID2D1DrawingStateBlock ** Block)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Description);
    UNREFERENCED_PARAMETER(Text);
    if(!Block)return E_INVALIDARG;
    *Block=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateWicBitmapRenderTarget(ID2D1Factory *This, IWICBitmap * Bitmap, CONST D2D1_RENDER_TARGET_PROPERTIES * Properties, ID2D1RenderTarget ** Target)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Bitmap);
    UNREFERENCED_PARAMETER(Properties);
    if(!Target)return E_INVALIDARG;
    *Target=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateHwndRenderTarget(ID2D1Factory *This, CONST D2D1_RENDER_TARGET_PROPERTIES * Properties, CONST D2D1_HWND_RENDER_TARGET_PROPERTIES * WindowProperties, ID2D1HwndRenderTarget ** Target)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Properties);
    UNREFERENCED_PARAMETER(WindowProperties);
    if(!Target)return E_INVALIDARG;
    *Target=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateDxgiSurfaceRenderTarget(ID2D1Factory *This, IDXGISurface * Surface, CONST D2D1_RENDER_TARGET_PROPERTIES * Properties, ID2D1RenderTarget ** Target)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Surface);
    UNREFERENCED_PARAMETER(Properties);
    if(!Target)return E_INVALIDARG;
    *Target=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateDCRenderTarget(ID2D1Factory *This, CONST D2D1_RENDER_TARGET_PROPERTIES * Properties, ID2D1DCRenderTarget ** Target)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Properties);
    if(!Target)return E_INVALIDARG;
    *Target=NULL;
    // A native resource would return the inner factory from GetFactory.
    // Keep this unimplemented until its resource identity is preserved.
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateStrokeStyle1(ID2D1Factory1 *This, CONST KX_STROKE_STYLE_PROPERTIES1 * Properties, CONST FLOAT * Dashes, UINT32 Count, ID2D1StrokeStyle1 ** Style)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Properties);
    UNREFERENCED_PARAMETER(Dashes);
    UNREFERENCED_PARAMETER(Count);
    if(!Style)return E_INVALIDARG;
    *Style=NULL;return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreatePathGeometry1(ID2D1Factory1 *This, ID2D1PathGeometry1 ** Geometry)
{
    UNREFERENCED_PARAMETER(This);
    if(!Geometry)return E_INVALIDARG;
    *Geometry=NULL;return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateDrawingStateBlock1(ID2D1Factory1 *This, CONST KX_DRAWING_STATE_DESCRIPTION1 * Description, IDWriteRenderingParams * Text, ID2D1DrawingStateBlock1 ** Block)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Description);
    UNREFERENCED_PARAMETER(Text);
    if(!Block)return E_INVALIDARG;
    *Block=NULL;return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_CreateGdiMetafile(ID2D1Factory1 *This, IStream * Stream, ID2D1GdiMetafile ** Metafile)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Stream);
    if(!Metafile)return E_INVALIDARG;
    *Metafile=NULL;return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxFactory_RegisterEffectFromStream(ID2D1Factory1 *This, REFCLSID ClassId, IStream * Xml, CONST KX_PROPERTY_BINDING * Bindings, UINT32 Count, KX_EFFECT_FACTORY Factory)
{
    ID2D1Factory1 *Native=((KX_D2D_FACTORY*)This)->Native;
    return ((KX_NATIVE_FACTORY1_VTBL*)Native->lpVtbl)->RegisterEffectFromStream(Native, ClassId, Xml, Bindings, Count, Factory);
}

static HRESULT STDMETHODCALLTYPE KxFactory_RegisterEffectFromString(ID2D1Factory1 *This, REFCLSID ClassId, PCWSTR Xml, CONST KX_PROPERTY_BINDING * Bindings, UINT32 Count, KX_EFFECT_FACTORY Factory)
{
    ID2D1Factory1 *Native=((KX_D2D_FACTORY*)This)->Native;
    return ((KX_NATIVE_FACTORY1_VTBL*)Native->lpVtbl)->RegisterEffectFromString(Native, ClassId, Xml, Bindings, Count, Factory);
}

static HRESULT STDMETHODCALLTYPE KxFactory_UnregisterEffect(ID2D1Factory1 *This, REFCLSID ClassId)
{
    ID2D1Factory1 *Native=((KX_D2D_FACTORY*)This)->Native;
    return ((KX_NATIVE_FACTORY1_VTBL*)Native->lpVtbl)->UnregisterEffect(Native, ClassId);
}

static HRESULT STDMETHODCALLTYPE KxFactory_GetRegisteredEffects(ID2D1Factory1 *This, CLSID * Effects, UINT32 Count, UINT32 * Returned, UINT32 * Registered)
{
    ID2D1Factory1 *Native=((KX_D2D_FACTORY*)This)->Native;
    return ((KX_NATIVE_FACTORY1_VTBL*)Native->lpVtbl)->GetRegisteredEffects(Native, Effects, Count, Returned, Registered);
}

static HRESULT STDMETHODCALLTYPE KxFactory_GetEffectProperties(ID2D1Factory1 *This, REFCLSID EffectId, ID2D1Properties ** Properties)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(EffectId);
    if(!Properties)return E_INVALIDARG;
    *Properties=NULL;return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxDevice_CreateDeviceContext(ID2D1Device *This, D2D1_DEVICE_CONTEXT_OPTIONS Options, ID2D1DeviceContext ** Context)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(Options);
    if(!Context)return E_INVALIDARG;
    *Context=NULL;return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE KxDevice_CreatePrintControl(ID2D1Device *This, IWICImagingFactory * WicFactory, IPrintDocumentPackageTarget * Document, CONST KX_PRINT_CONTROL_PROPERTIES * Properties, ID2D1PrintControl ** Control)
{
    UNREFERENCED_PARAMETER(This);
    UNREFERENCED_PARAMETER(WicFactory);
    UNREFERENCED_PARAMETER(Document);
    UNREFERENCED_PARAMETER(Properties);
    if(!Control)return E_INVALIDARG;
    *Control=NULL;return E_NOTIMPL;
}

static VOID STDMETHODCALLTYPE KxDevice_SetMaximumTextureMemory(ID2D1Device *This, UINT64 Bytes)
{
    ID2D1Device *Native=((KX_D2D_DEVICE*)This)->Native;
    ((KX_NATIVE_DEVICE_VTBL*)Native->lpVtbl)->SetMaximumTextureMemory(Native, Bytes);
}

static UINT64 STDMETHODCALLTYPE KxDevice_GetMaximumTextureMemory(ID2D1Device *This)
{
    ID2D1Device *Native=((KX_D2D_DEVICE*)This)->Native;
    return ((KX_NATIVE_DEVICE_VTBL*)Native->lpVtbl)->GetMaximumTextureMemory(Native);
}

static VOID STDMETHODCALLTYPE KxDevice_ClearResources(ID2D1Device *This, UINT32 Milliseconds)
{
    ID2D1Device *Native=((KX_D2D_DEVICE*)This)->Native;
    ((KX_NATIVE_DEVICE_VTBL*)Native->lpVtbl)->ClearResources(Native, Milliseconds);
}

static CONST KX_FACTORY2_VTBL KxD2DFactoryVtbl={
    {
        {{KxD2DFactory_QI,KxD2DFactory_AddRef,KxD2DFactory_Release},
         KxFactory_ReloadSystemMetrics,
         KxFactory_GetDesktopDpi,
         KxFactory_CreateRectangleGeometry,
         KxFactory_CreateRoundedRectangleGeometry,
         KxFactory_CreateEllipseGeometry,
         KxFactory_CreateGeometryGroup,
         KxFactory_CreateTransformedGeometry,
         KxFactory_CreatePathGeometry,
         KxFactory_CreateStrokeStyle,
         KxFactory_CreateDrawingStateBlock,
         KxFactory_CreateWicBitmapRenderTarget,
         KxFactory_CreateHwndRenderTarget,
         KxFactory_CreateDxgiSurfaceRenderTarget,
         KxFactory_CreateDCRenderTarget},
        KxD2DFactory_CreateDevice,
        KxFactory_CreateStrokeStyle1,
        KxFactory_CreatePathGeometry1,
        KxFactory_CreateDrawingStateBlock1,
        KxFactory_CreateGdiMetafile,
        KxFactory_RegisterEffectFromStream,
        KxFactory_RegisterEffectFromString,
        KxFactory_UnregisterEffect,
        KxFactory_GetRegisteredEffects,
        KxFactory_GetEffectProperties
    },
    ID2D1Factory2_CreateDevice
};

static CONST KX_DEVICE1_VTBL KxD2DDeviceVtbl={
    {
        {{KxD2DDevice_QI,KxD2DDevice_AddRef,KxD2DDevice_Release},KxD2DDevice_GetFactory},
        KxDevice_CreateDeviceContext,
        KxDevice_CreatePrintControl,
        KxDevice_SetMaximumTextureMemory,
        KxDevice_GetMaximumTextureMemory,
        KxDevice_ClearResources
    },
    ID2D1Device1_GetRenderingPriority,
    ID2D1Device1_SetRenderingPriority,
    ID2D1Device1_CreateDeviceContext
};
