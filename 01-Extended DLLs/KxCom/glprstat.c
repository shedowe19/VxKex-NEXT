///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     glprstat.c
//
// Abstract:
//
//     Implementation of IGlobalizationPreferencesStatics and all the other
//     crappy boilerplate interfaces and functions that go along with it. Some Qt
//     applications require this interface and will crash if it's not available.
//
//     Can't emphasize how much I hate COM, WinRT, and C++ bullshit
//     STOP USING THESE FUCKING CLASSES
//     FUCKING NIGGERS
//     GetUserPreferredUILanguages IS RIGHT THERE DIPSHIT! USE IT!
//
// Author:
//
//     vxiiduu (18-Feb-2024)
//
// Environment:
//
//     inside some bloatware c++ junk
//
// Revision History:
//
//     vxiiduu              18-Feb-2024  Initial creation.
//     vxiiduu              03-Mar-2024  Increase the level of implementation
//                                       so that this interface is no longer for
//                                       ASH usage only.
//
///////////////////////////////////////////////////////////////////////////////

#include "buildcfg.h"
#include "kxcomp.h"

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_QueryInterface(
	IN	IVectorView_HSTRING	*This,
	IN	REFIID				RefIID,
	OUT	PPVOID				Interface)
{
	ASSERT (This != NULL);
	ASSERT (RefIID != NULL);
	ASSERT (Interface != NULL);

	*Interface = NULL;
	if (IsEqualIID(RefIID, &IID_IMarshal)) {
		return This->Marshaler->lpVtbl->QueryInterface(This->Marshaler, RefIID, Interface);
	}

	if (IsEqualIID(RefIID, &IID_IUnknown) ||
		IsEqualIID(RefIID, &IID_IAgileObject) ||
		IsEqualIID(RefIID, &IID_IInspectable) ||
		IsEqualIID(RefIID, &IID_IVectorView)) {

		This->lpVtbl->AddRef(This);
		*Interface = This;
	} else {
		return E_NOINTERFACE;
	}

	return S_OK;
}

STATIC ULONG STDMETHODCALLTYPE CVectorView_HSTRING_AddRef(
	IN	IVectorView_HSTRING	*This)
{
	ASSERT (This != NULL);
	return InterlockedIncrement(&This->RefCount);
}

STATIC ULONG STDMETHODCALLTYPE CVectorView_HSTRING_Release(
	IN	IVectorView_HSTRING	*This)
{
	LONG NewRefCount;

	ASSERT (This != NULL);

	NewRefCount = InterlockedDecrement(&This->RefCount);

	if (NewRefCount == 0) {
		if (This->Marshaler) This->Marshaler->lpVtbl->Release(This->Marshaler);
		until (This->NumberOfHstrings == 0) {
			// get rid of all hstrings in array
			WindowsDeleteString(This->HstringArray[--This->NumberOfHstrings]);
		}

		SafeFree(This->HstringArray);
		SafeFree(This);
	}

	return NewRefCount;
}

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_GetIids(
	IN	IVectorView_HSTRING	*This,
	OUT	PULONG				NumberOfIids,
	OUT	IID					**IidArray)
{
	IID *Array;
	ULONG Count;

	ASSERT (NumberOfIids != NULL);
	ASSERT (IidArray != NULL);

	Count = 1;

	Array = (IID *) CoTaskMemAlloc(Count * sizeof(IID));
	if (!Array) {
		return E_OUTOFMEMORY;
	}

	*NumberOfIids = Count;
	*IidArray = Array;
	Array[0] = IID_IVectorView;

	return S_OK;
}

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_GetRuntimeClassName(
	IN	IVectorView_HSTRING	*This,
	OUT	HSTRING				*ClassName)
{
	PCWSTR Name = L"Windows.Foundation.Collections.IVector";
	ASSERT (ClassName != NULL);
	return WindowsCreateString(Name, (ULONG) wcslen(Name), ClassName);
}

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_GetTrustLevel(
	IN	IVectorView_HSTRING	*This,
	OUT	TrustLevel			*TrustLevel)
{
	ASSERT (TrustLevel != NULL);
	*TrustLevel = BaseTrust;
	return S_OK;
}

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_GetAt(
	IN	IVectorView_HSTRING	*This,
	IN	ULONG				Index,
	OUT	HSTRING				*Value)
{
	ASSERT (This != NULL);
	ASSERT (Value != NULL);

	*Value = NULL;

	if (Index >= This->NumberOfHstrings) {
		return E_BOUNDS;
	}

	return WindowsDuplicateString(This->HstringArray[Index], Value);
}

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_get_Size(
	IN	IVectorView_HSTRING	*This,
	OUT	PULONG				NumberOfElements)
{
	ASSERT (This != NULL);
	ASSERT (NumberOfElements != NULL);

	*NumberOfElements = This->NumberOfHstrings;
	return S_OK;
}

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_IndexOf(
	IN	IVectorView_HSTRING	*This,
	IN	HSTRING				String,
	OUT	PULONG				IndexOut,
	OUT	PBOOLEAN			WasFound)
{
	ULONG Index;

	ASSERT (This != NULL);
	ASSERT (IndexOut != NULL);
	ASSERT (WasFound != NULL);

	for (Index = 0; Index < This->NumberOfHstrings; ++Index) {
		HRESULT Result;
		INT ComparisonResult;

		Result = WindowsCompareStringOrdinal(
			String,
			This->HstringArray[Index],
			&ComparisonResult);

		if (SUCCEEDED(Result) && ComparisonResult == 0) {
			*IndexOut = Index;
			*WasFound = TRUE;
			return S_OK;
		}
	}

	*IndexOut = 0;
	*WasFound = FALSE;
	return S_OK;
}

STATIC HRESULT STDMETHODCALLTYPE CVectorView_HSTRING_GetMany(
	IN	IVectorView_HSTRING	*This,
	IN	ULONG				StartIndex,
	IN	ULONG				NumberOfItems,
	OUT	HSTRING				*Items,
	OUT	PULONG				NumberOfItemsOut)
{
	ULONG Index, Count;
	if (!NumberOfItemsOut) return E_POINTER;
	*NumberOfItemsOut = 0;
	if (StartIndex > This->NumberOfHstrings) return E_BOUNDS;
	if (!NumberOfItems) return S_OK;
	if (!Items) return E_POINTER;
	Count = min(NumberOfItems, This->NumberOfHstrings - StartIndex);
	for (Index = 0; Index < Count; ++Index) {
		HRESULT Result;

		Result = WindowsDuplicateString(
			This->HstringArray[StartIndex + Index],
			&Items[Index]);

		if (FAILED(Result)) {
			// deallocate all strings that were created up to this point and return
			// a failure status

			until (Index == 0) {
				--Index;
				WindowsDeleteString(Items[Index]);
				Items[Index] = NULL;
			}

			*NumberOfItemsOut = 0;
			return Result;
		}

		++*NumberOfItemsOut;

	}

	return S_OK;
}

IVectorView_HSTRINGVtbl CVectorView_HSTRINGVtbl = {
	CVectorView_HSTRING_QueryInterface,
	CVectorView_HSTRING_AddRef,
	CVectorView_HSTRING_Release,
	
	CVectorView_HSTRING_GetIids,
	CVectorView_HSTRING_GetRuntimeClassName,
	CVectorView_HSTRING_GetTrustLevel,

	CVectorView_HSTRING_GetAt,
	CVectorView_HSTRING_get_Size,
	CVectorView_HSTRING_IndexOf,
	CVectorView_HSTRING_GetMany,
};

IVectorView_HSTRING CVectorView_HSTRING_Template = {
	&CVectorView_HSTRINGVtbl,	// lpVtbl
	1,							// RefCount
};

typedef struct _IGlobalizationPreferencesStatics IGlobalizationPreferencesStatics;

STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_QueryInterface(
	IN	IGlobalizationPreferencesStatics	*This,
	IN	REFIID								RefIID,
	OUT	PPVOID								Interface)
{
	UNREFERENCED_PARAMETER(This);
	return KxCompQueryBuiltinFactory(KxRuntimeGlobalizationPreferences, RefIID, Interface);
}

STATIC ULONG STDMETHODCALLTYPE CGlobalizationPreferencesStatics_AddRef(
	IN	IGlobalizationPreferencesStatics	*This)
{
	return 1;
}

STATIC ULONG STDMETHODCALLTYPE CGlobalizationPreferencesStatics_Release(
	IN	IGlobalizationPreferencesStatics	*This)
{
	return 1;
}

STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_GetIids(
	IN	IGlobalizationPreferencesStatics	*This,
	OUT	PULONG				NumberOfIids,
	OUT	IID					**IidArray)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeGlobalizationPreferences, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetIids(Factory, NumberOfIids, IidArray);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_GetRuntimeClassName(
	IN	IGlobalizationPreferencesStatics	*This,
	OUT	HSTRING								*ClassName)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeGlobalizationPreferences, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetRuntimeClassName(Factory, ClassName);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_GetTrustLevel(
	IN	IGlobalizationPreferencesStatics	*This,
	OUT	TrustLevel							*Level)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeGlobalizationPreferences, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetTrustLevel(Factory, Level);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

// Couldn't be bothered to implement the rest.
// I'll come back and finish this bullshit off if I see apps using them
STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_NotImplemented(
	IN	IGlobalizationPreferencesStatics	*This,
	OUT	IVectorView_HSTRING					**VectorView)
{
	KexLogWarningEvent(L"Unimplemented method of IGlobalizationPreferencesStatics called");
	return E_NOTIMPL;
}

STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_get_Languages(
	IN	IGlobalizationPreferencesStatics	*This,
	OUT	IVectorView_HSTRING					**VectorView)
{
	HRESULT Result;
	WCHAR LocaleName[LOCALE_NAME_MAX_LENGTH];
	ULONG LocaleNameCch;
	HSTRING LocaleNameHstring;
	IVectorView_HSTRING *NewVectorView;
	if (!VectorView) return E_POINTER;
	*VectorView = NULL;

	//
	// Get the user's locale name and make a HSTRING out of it.
	//

	LocaleNameCch = ARRAYSIZE(LocaleName);
	LocaleNameCch = GetUserDefaultLocaleName(LocaleName, LocaleNameCch);
	ASSERT (LocaleNameCch != 0);

	if (LocaleNameCch == 0) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	Result = WindowsCreateString(LocaleName, LocaleNameCch - 1, &LocaleNameHstring);
	ASSERT (SUCCEEDED(Result));

	if (FAILED(Result)) {
		return Result;
	}

	//
	// Create an IVectorView<HSTRING> interface and put the previously created
	// HSTRING into it.
	//

	NewVectorView = SafeAlloc(IVectorView_HSTRING, 1);
	if (!NewVectorView) {
		WindowsDeleteString(LocaleNameHstring);
		return E_OUTOFMEMORY;
	}

	*NewVectorView = CVectorView_HSTRING_Template;
	NewVectorView->NumberOfHstrings = 1;
	NewVectorView->HstringArray = SafeAlloc(HSTRING, NewVectorView->NumberOfHstrings);

	if (!NewVectorView->HstringArray) {
		WindowsDeleteString(LocaleNameHstring);
		SafeFree(NewVectorView);
		return E_OUTOFMEMORY;
	}
	
	NewVectorView->HstringArray[0] = LocaleNameHstring;
	Result = CoCreateFreeThreadedMarshaler((IUnknown *) NewVectorView, &NewVectorView->Marshaler);
	if (FAILED(Result)) {
		NewVectorView->lpVtbl->Release(NewVectorView);
		return Result;
	}
	*VectorView = NewVectorView;
	return S_OK;
}

STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_get_HomeGeographicRegion(
	IN	IGlobalizationPreferencesStatics	*This,
	OUT	HSTRING								*Country)
{
	ULONG ResultCch;
	WCHAR CountryName[8];

	ASSERT (Country != NULL);
	
	ResultCch = GetLocaleInfoEx(
		LOCALE_NAME_USER_DEFAULT,
		LOCALE_ICOUNTRY,
		CountryName,
		ARRAYSIZE(CountryName));

	ASSERT (ResultCch != 0);

	if (ResultCch == 0) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	return WindowsCreateString(CountryName, (ULONG) wcslen(CountryName), Country);
}

STATIC HRESULT STDMETHODCALLTYPE CGlobalizationPreferencesStatics_get_WeekStartsOn(
	IN	IGlobalizationPreferencesStatics	*This,
	OUT	DayOfWeek							*StartsOn)
{
	ULONG ResultCch;
	ULONG WeekStartsOn;

	ASSERT (StartsOn != NULL);

	ResultCch = GetLocaleInfoEx(
		LOCALE_NAME_USER_DEFAULT,
		LOCALE_RETURN_NUMBER | LOCALE_IFIRSTDAYOFWEEK,
		(PWSTR) &WeekStartsOn,
		2);

	ASSERT (ResultCch == 2);
	ASSERT (WeekStartsOn <= 6);

	if (ResultCch == 0) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	// Retarded WinRT people thought it was a great idea to use numerical constants
	// that are different from what is returned by existing functions.
	if (WeekStartsOn == 6) {
		*StartsOn = DayOfWeek_Sunday;
	} else {
		*StartsOn = (DayOfWeek) (WeekStartsOn + 1);
	}

	return S_OK;
}

IGlobalizationPreferencesStaticsVtbl CGlobalizationPreferencesStaticsVtbl = {
	CGlobalizationPreferencesStatics_QueryInterface,
	CGlobalizationPreferencesStatics_AddRef,
	CGlobalizationPreferencesStatics_Release,

	CGlobalizationPreferencesStatics_GetIids,
	CGlobalizationPreferencesStatics_GetRuntimeClassName,
	CGlobalizationPreferencesStatics_GetTrustLevel,

	CGlobalizationPreferencesStatics_NotImplemented,
	CGlobalizationPreferencesStatics_NotImplemented,
	CGlobalizationPreferencesStatics_NotImplemented,
	CGlobalizationPreferencesStatics_get_Languages,
	CGlobalizationPreferencesStatics_get_HomeGeographicRegion,
	CGlobalizationPreferencesStatics_get_WeekStartsOn
};

IGlobalizationPreferencesStatics CGlobalizationPreferencesStatics = {
	&CGlobalizationPreferencesStaticsVtbl
};
