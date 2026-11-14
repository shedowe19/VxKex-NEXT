#include "buildcfg.h"
#include "kxdxp.h"

KXDXAPI HRESULT WINAPI DCompositionCreateDevice(
	IN	IDXGIDevice	*DXGIDevice OPTIONAL,
	IN	REFIID		RefIID,
	OUT	PPVOID		DCompositionDevice)
{
	if (!DCompositionDevice) {
		return E_INVALIDARG;
	}

	KexLogWarningEvent(L"Unimplemented function DCompositionCreateDevice called");
	*DCompositionDevice = NULL;
	return E_NOINTERFACE;
}

KXDXAPI HRESULT WINAPI DCompositionCreateDevice2(
	IN	IUnknown	*RenderingDevice OPTIONAL,
	IN	REFIID		RefIID,
	OUT	PPVOID		DCompositionDevice)
{
	if (!DCompositionDevice) {
		return E_INVALIDARG;
	}

	KexLogWarningEvent(L"Unimplemented function DCompositionCreateDevice2 called");
	*DCompositionDevice = NULL;
	return E_NOINTERFACE;
}

KXDXAPI HRESULT WINAPI DCompositionCreateDevice3(
	IN	IUnknown	*RenderingDevice OPTIONAL,
	IN	REFIID		RefIID,
	OUT	PPVOID		DCompositionDevice)
{
	if (!DCompositionDevice) {
		return E_INVALIDARG;
	}

	KexLogWarningEvent(L"Unimplemented function DCompositionCreateDevice3 called");
	*DCompositionDevice = NULL;
	return E_NOINTERFACE;
}

// No composition-surface or decode-swapchain backend exists on Windows 7.
// An immortal fake factory whose every operation fails is not an implementation.
HRESULT WINAPI CreateIDXGIFactoryMedia(OUT PPVOID FactoryMedia)
{
    if (!FactoryMedia) {
        return E_INVALIDARG;
    }
    *FactoryMedia = NULL;
    return E_NOINTERFACE;
}
