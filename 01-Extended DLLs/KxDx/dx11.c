#include "buildcfg.h"
#include "kxdxp.h"
#include <d3d11.h>

#if defined(KEX_TARGET_TYPE_EXE) || defined(KEX_TARGET_TYPE_DLL)
#  if defined(KEX_ARCH_X64)
#    pragma comment(lib, "d3d11_x64.lib")
#  elif defined(KEX_ARCH_X86)
#    pragma comment(lib, "d3d11_x86.lib")
#  endif
#endif

// Keep native D3D11 semantics, but never give a native driver an outer COM
// adapter with a different private ABI. Only our exact adapter vtable is
// recognized. The temporary native reference also covers reentrant destruction
// of the outer adapter while the native API runs.
HRESULT WINAPI Ext_D3D11CreateDevice(
	IDXGIAdapter *Adapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL *FeatureLevels,
	UINT FeatureLevelCount,
	UINT SDKVersion,
	ID3D11Device **Device,
	D3D_FEATURE_LEVEL *ChosenFeatureLevel,
	ID3D11DeviceContext **ImmediateContext)
{
	IDXGIAdapter *Native;
	BOOL Pinned;
	HRESULT Result;
	ULONG LastError;

	Native=KxPinNativeDXGIAdapter(Adapter,&Pinned);
	__try {
		Result=D3D11CreateDevice(Native,DriverType,Software,Flags,FeatureLevels,
			FeatureLevelCount,SDKVersion,Device,ChosenFeatureLevel,ImmediateContext);
	} __finally {
		if(Pinned) {
			LastError=GetLastError();
			Native->lpVtbl->Release(Native);
			SetLastError(LastError);
		}
	}
	return Result;
}

HRESULT WINAPI Ext_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter *Adapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL *FeatureLevels,
	UINT FeatureLevelCount,
	UINT SDKVersion,
	const DXGI_SWAP_CHAIN_DESC *SwapChainDesc,
	IDXGISwapChain **SwapChain,
	ID3D11Device **Device,
	D3D_FEATURE_LEVEL *ChosenFeatureLevel,
	ID3D11DeviceContext **ImmediateContext)
{
	IDXGIAdapter *Native;
	BOOL Pinned;
	HRESULT Result;
	ULONG LastError;

	Native=KxPinNativeDXGIAdapter(Adapter,&Pinned);
	__try {
		Result=D3D11CreateDeviceAndSwapChain(Native,DriverType,Software,Flags,
			FeatureLevels,FeatureLevelCount,SDKVersion,SwapChainDesc,SwapChain,
			Device,ChosenFeatureLevel,ImmediateContext);
	} __finally {
		if(Pinned) {
			LastError=GetLastError();
			Native->lpVtbl->Release(Native);
			SetLastError(LastError);
		}
	}
	return Result;
}

KXDXAPI HRESULT WINAPI D3D11On12CreateDevice(
	IN	IUnknown				*pDevice,
	UINT						Flags,
	IN	const D3D_FEATURE_LEVEL	*pFeatureLevels	OPTIONAL,
	UINT						FeatureLevels,
	IN	IUnknown				* const	*ppCommandQueues	OPTIONAL,
	UINT						NumQueues,
	UINT						NodeMask,
	OUT	ID3D11Device			**ppDevice OPTIONAL,
	OUT	ID3D11DeviceContext		**ppImmediateContext	OPTIONAL,
	OUT	D3D_FEATURE_LEVEL		*pChosenFeatureLevel	OPTIONAL)
{
	if (ppDevice) {
		*ppDevice = NULL;
	}
	if (ppImmediateContext) {
		*ppImmediateContext = NULL;
	}

	// This API wraps an existing ID3D12Device and its command queues. Creating
	// an unrelated D3D11 hardware device cannot satisfy that contract: resources,
	// queue ordering and ID3D11On12Device would all be missing. In particular,
	// pDevice is not an IDXGIAdapter, and pChosenFeatureLevel is output only.
	// Preserve the caller's feature-level output on failure.
	KexLogWarningEvent(L"D3D11On12CreateDevice requires an unavailable D3D12/11-on-12 backend");
	return DXGI_ERROR_UNSUPPORTED;
}
