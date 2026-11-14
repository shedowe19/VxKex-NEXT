#include "buildcfg.h"
#include "kxcomp.h"

KXCOMAPI HRESULT WINAPI RoGetActivationFactory(
	IN	HSTRING	ActivatableClassId,
	IN	REFIID	RefIID,
	OUT	PPVOID	Factory)
{
	IActivationFactory *ClassFactory;
	HRESULT Result;
	if (!Factory) return E_POINTER;
	*Factory = NULL;
	if (!RefIID) return E_INVALIDARG;
	Result = KxCompResolveActivationFactory(ActivatableClassId, &ClassFactory);
	if (FAILED(Result)) return Result;
	Result = ClassFactory->lpVtbl->QueryInterface(ClassFactory, RefIID, Factory);
	ClassFactory->lpVtbl->Release(ClassFactory);
	return Result;
}

// Added for Cavalry
KXCOMAPI HRESULT WINAPI RoRegisterActivationFactories(
	IN	HSTRING					*ActivatableClassIds,
	IN	PPVOID					ActivationFactoryCallbacks,
	IN	ULONG					Count,
	OUT	PRO_REGISTRATION_COOKIE	Cookie)
{
	KexLogWarningEvent(L"Unimplemented function RoRegisterActivationFactories called.");
	return E_NOTIMPL;
}

// Added for Cavalry
KXCOMAPI VOID WINAPI RoRevokeActivationFactories(
	IN	RO_REGISTRATION_COOKIE	Cookie)
{
	KexLogWarningEvent(L"Unimplemented function RoRevokeActivationFactories called.");
	return;
}

KXCOMAPI HRESULT WINAPI RoActivateInstance(
	IN	HSTRING			ActivatableClassId,
	OUT	IInspectable	**Instance)
{
	IActivationFactory *Factory;
	HRESULT Result;
	if (!Instance) return E_POINTER;
	*Instance = NULL;
	Result = RoGetActivationFactory(ActivatableClassId, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->ActivateInstance(Factory, Instance);
	Factory->lpVtbl->Release(Factory);
	return Result;
}
