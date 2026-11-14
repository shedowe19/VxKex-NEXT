#pragma once

#include <KexComm.h>
#include <KexDll.h>
#include <KxCom.h>

EXTERN PKEX_PROCESS_DATA KexData;

typedef enum _KX_BUILTIN_RUNTIME_CLASS {
	KxRuntimeUri,
	KxRuntimeUISettings,
	KxRuntimeGlobalizationPreferences,
	KxRuntimeUIViewSettings,
	KxRuntimeLauncher,
	KxRuntimeClassCount
} KX_BUILTIN_RUNTIME_CLASS;

HRESULT KxCompResolveActivationFactory(HSTRING ClassId, IActivationFactory **Factory);
HRESULT KxCompQueryBuiltinFactory(KX_BUILTIN_RUNTIME_CLASS Class, REFIID Iid, PPVOID Object);
HRESULT KxCompCreateCompletedBooleanOperation(BOOLEAN Value, IAsyncOperation **Operation);
HRESULT KxCompQueryCachedMarshaler(ULONG Slot, IUnknown *Outer, REFIID Iid, PPVOID Object);

VOID WINAPI KxCompTryInheritServiceForClass(REFCLSID ClassId, ULONG ClassContext);

KXCOMAPI HRESULT STDMETHODCALLTYPE CActivationFactory_QueryInterface(
	IN	IActivationFactory	*This,
	IN	REFIID				RefIID,
	OUT	PPVOID				Object);
