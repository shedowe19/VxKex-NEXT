#include "buildcfg.h"
#include "kxcomp.h"

STATIC IActivationFactory ClassFactories[KxRuntimeClassCount];
STATIC IUnknown *ClassMarshalers[KxRuntimeClassCount + 2];

// Immutable built-ins are process-lifetime objects. Their aggregating FTM is
// created outside locks; losing concurrent constructors release their own FTM.
HRESULT KxCompQueryCachedMarshaler(ULONG Slot, IUnknown *Outer, REFIID Iid, PPVOID Object)
{
	IUnknown *Marshaler, *Created;
	HRESULT Result;
	if (!Object) return E_POINTER;
	*Object = NULL;
	if (Slot >= ARRAYSIZE(ClassMarshalers)) return E_NOINTERFACE;
	Marshaler = InterlockedCompareExchangePointer((PVOID volatile *) &ClassMarshalers[Slot], NULL, NULL);
	if (!Marshaler) {
		Result = CoCreateFreeThreadedMarshaler(Outer, &Created);
		if (FAILED(Result)) return Result;
		Marshaler = InterlockedCompareExchangePointer((PVOID volatile *) &ClassMarshalers[Slot], Created, NULL);
		if (Marshaler) Created->lpVtbl->Release(Created);
		else Marshaler = Created;
	}
	return Marshaler->lpVtbl->QueryInterface(Marshaler, Iid, Object);
}

STATIC KX_BUILTIN_RUNTIME_CLASS KxCompFactoryClass(IActivationFactory *Factory)
{
	ULONG Index;
	for (Index = 0; Index < KxRuntimeClassCount; ++Index) {
		if (Factory == &ClassFactories[Index]) return (KX_BUILTIN_RUNTIME_CLASS) Index;
	}
	return KxRuntimeClassCount;
}

//
// Implements the IActivationFactory interface that is returned by
// RoGetActivationFactory.
//

KXCOMAPI HRESULT STDMETHODCALLTYPE CActivationFactory_QueryInterface(
	IN	IActivationFactory	*This,
	IN	REFIID				RefIID,
	OUT	PPVOID				Object)
{
	KX_BUILTIN_RUNTIME_CLASS Class = KxCompFactoryClass(This);
	if (!Object) return E_POINTER;
	*Object = NULL;
	if (!RefIID) return E_INVALIDARG;
	if (IsEqualIID(RefIID, &IID_IMarshal)) {
		if ((ULONG) Class >= KxRuntimeClassCount) return E_NOINTERFACE;
		return KxCompQueryCachedMarshaler(Class, (IUnknown *) This, RefIID, Object);
	}

	if (IsEqualIID(RefIID, &IID_IUnknown) ||
		IsEqualIID(RefIID, &IID_IAgileObject) ||
		IsEqualIID(RefIID, &IID_IInspectable) ||
		IsEqualIID(RefIID, &IID_IActivationFactory)) {

		*Object = This;
	} else if (Class == KxRuntimeGlobalizationPreferences && IsEqualIID(RefIID, &IID_IGlobalizationPreferencesStatics)) {
		*Object = &CGlobalizationPreferencesStatics;
	} else if (Class == KxRuntimeUIViewSettings && IsEqualIID(RefIID, &IID_IUIViewSettingsInterop)) {
		*Object = &CUIViewSettingsInterop;
	} else if (Class == KxRuntimeUri && IsEqualIID(RefIID, &IID_IUriRuntimeClassFactory)) {
		*Object = &CUriRuntimeClassFactory;
	} else if (Class == KxRuntimeLauncher && IsEqualIID(RefIID, &IID_ILauncherStatics)) {
		*Object = &CLauncherStatics;
	} else {
		return E_NOINTERFACE;
	}
	((IUnknown *) *Object)->lpVtbl->AddRef((IUnknown *) *Object);
	return S_OK;
}

KXCOMAPI ULONG STDMETHODCALLTYPE CActivationFactory_AddRef(
	IN	IActivationFactory	*This)
{
	return 1;
}

KXCOMAPI ULONG STDMETHODCALLTYPE CActivationFactory_Release(
	IN	IActivationFactory	*This)
{
	return 1;
}

KXCOMAPI HRESULT STDMETHODCALLTYPE CActivationFactory_GetIids(
	IN	IActivationFactory	*This,
	OUT	PULONG				NumberOfIids,
	OUT	IID					**IidArray)
{
	IID *Array;
	ULONG Count;
	const IID *Extra = NULL;
	KX_BUILTIN_RUNTIME_CLASS Class = KxCompFactoryClass(This);

	if (!NumberOfIids || !IidArray) return E_POINTER;
	*NumberOfIids = 0;
	*IidArray = NULL;
	if (Class == KxRuntimeUri) Extra = &IID_IUriRuntimeClassFactory;
	else if (Class == KxRuntimeGlobalizationPreferences) Extra = &IID_IGlobalizationPreferencesStatics;
	else if (Class == KxRuntimeLauncher) Extra = &IID_ILauncherStatics;
	else if (Class == KxRuntimeUIViewSettings) Extra = &IID_IUIViewSettingsInterop;
	Count = Extra ? 2 : 1;

	Array = (IID *) CoTaskMemAlloc(Count * sizeof(IID));
	if (!Array) {
		return E_OUTOFMEMORY;
	}

	*NumberOfIids = Count;
	*IidArray = Array;
	Array[0] = IID_IActivationFactory;
	if (Extra) Array[1] = *Extra;

	return S_OK;
}

KXCOMAPI HRESULT STDMETHODCALLTYPE CActivationFactory_GetRuntimeClassName(
	IN	IActivationFactory	*This,
	OUT	HSTRING				*ClassName)
{
	UNREFERENCED_PARAMETER(This);
	if (!ClassName) return E_POINTER;
	*ClassName = NULL;
	return (HRESULT) 0x8000000EL; // E_ILLEGAL_METHOD_CALL, as on native factories.
}

KXCOMAPI HRESULT STDMETHODCALLTYPE CActivationFactory_GetTrustLevel(
	IN	IActivationFactory	*This,
	OUT	TrustLevel			*Level)
{
	if (!Level) return E_POINTER;
	*Level = BaseTrust;
	return S_OK;
}

KXCOMAPI HRESULT STDMETHODCALLTYPE CActivationFactory_ActivateInstance(
	IN	IActivationFactory	*This,
	OUT	IInspectable		**Object)
{
	if (!Object) return E_POINTER;
	*Object = NULL;
	if (KxCompFactoryClass(This) == KxRuntimeUISettings) {
		return CUISettings.lpVtbl->QueryInterface(&CUISettings, &IID_IInspectable, (PPVOID) Object);
	}
	return E_NOTIMPL; // Argument-requiring Uri and static-only runtime classes.
}

IActivationFactoryVtbl CActivationFactoryVtbl = {
	CActivationFactory_QueryInterface,
	CActivationFactory_AddRef,
	CActivationFactory_Release,
	CActivationFactory_GetIids,
	CActivationFactory_GetRuntimeClassName,
	CActivationFactory_GetTrustLevel,
	CActivationFactory_ActivateInstance
};

IActivationFactory CActivationFactory = {
	&CActivationFactoryVtbl
};

// Immutable process-lifetime factories keep COM identity separate per class.
STATIC IActivationFactory ClassFactories[KxRuntimeClassCount] = {
	{&CActivationFactoryVtbl}, {&CActivationFactoryVtbl}, {&CActivationFactoryVtbl},
	{&CActivationFactoryVtbl}, {&CActivationFactoryVtbl}
};

HRESULT KxCompQueryBuiltinFactory(KX_BUILTIN_RUNTIME_CLASS Class, REFIID Iid, PPVOID Object)
{
	if (!Object) return E_POINTER;
	*Object = NULL;
	if ((ULONG) Class >= KxRuntimeClassCount) return E_INVALIDARG;
	return CActivationFactory_QueryInterface(&ClassFactories[Class], Iid, Object);
}

HRESULT KxCompResolveActivationFactory(HSTRING ClassId, IActivationFactory **Factory)
{
	STATIC PCWSTR Names[KxRuntimeClassCount] = {
		L"Windows.Foundation.Uri", L"Windows.UI.ViewManagement.UISettings",
		L"Windows.System.UserProfile.GlobalizationPreferences",
		L"Windows.UI.ViewManagement.UIViewSettings", L"Windows.System.Launcher"
	};
	PCWSTR Name;
	ULONG Length, Index, Character;
	ULONG_PTR Context;
	HRESULT Result;
	if (!Factory) return E_POINTER;
	*Factory = NULL;
	Name = WindowsGetStringRawBuffer(ClassId, &Length);
	if (!Length || Length > MAXLONG) return E_INVALIDARG;
	for (Character = 0; Character < Length; ++Character) {
		if (!Name[Character]) return E_INVALIDARG;
	}
	Result = CoGetContextToken(&Context);
	if (FAILED(Result)) return Result;
	for (Index = 0; Index < KxRuntimeClassCount; ++Index) {
		if (CompareStringOrdinal(Name, (INT) Length, Names[Index], -1, FALSE) == CSTR_EQUAL) {
			*Factory = &ClassFactories[Index];
			(*Factory)->lpVtbl->AddRef(*Factory);
			return S_OK;
		}
	}
	return REGDB_E_CLASSNOTREG;
}
