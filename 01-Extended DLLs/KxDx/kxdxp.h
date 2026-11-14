#pragma once

#include <KexComm.h>
#include <KexDll.h>
#include <KxDx.h>

EXTERN PKEX_PROCESS_DATA KexData;

typedef interface IMFMediaBuffer IMFMediaBuffer;
typedef interface IMFDXGIDeviceManager IMFDXGIDeviceManager;

//
// dcmpstub.c
//

HRESULT WINAPI CreateIDXGIFactoryMedia(
	OUT	PPVOID	FactoryMedia);

//
// direct2d.c
//

HRESULT WrapDirect2DDevice(
	IN OUT	ID2D1Device **Device,
	IN		ID2D1Factory2 *Owner);

HRESULT WrapDirect2DFactory(
	IN OUT	ID2D1Factory1 **Factory);

//
// dxgi.c
//

HRESULT WrapDXGIFactory(
	IN OUT	IDXGIFactory2 **Factory,
	IN		ULONG CreationFlags);

// A borrowed foreign/null adapter passes through unchanged. Our own adapter is
// unwrapped and AddRef-pinned until the caller releases the native reference.
// Additional native AddRef must not change the API caller's LastError.
IDXGIAdapter *KxPinNativeDXGIAdapter(
	IN	IDXGIAdapter *Adapter,
	OUT	PBOOL Pinned);

//
// rptnoint.c
//

VOID ReportNoInterfaceError(
	IN	REFIID	RefIID,
	IN	HRESULT	Result,
	IN	PCWSTR	ErrorMessage);
