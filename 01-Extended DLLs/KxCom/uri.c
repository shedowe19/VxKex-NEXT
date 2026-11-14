#include "buildcfg.h"
#include "kxcomp.h"

STATIC HRESULT KxCompValidateUriString(HSTRING String, BOOL AllowEmpty)
{
	// Native WinRT Uri uses the NUL-terminated URL parser even when the HSTRING
	// extends beyond an embedded NUL. Match that measured constructor behavior;
	// runtime class identifiers are validated separately using their full length.
	return !AllowEmpty && WindowsIsStringEmpty(String) ? E_INVALIDARG : S_OK;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_QueryInterface(
	IN	IUriRuntimeClass	*This,
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
		IsEqualIID(RefIID, &IID_IUriRuntimeClass)) {

		*Interface = This;
		InterlockedIncrement(&This->RefCount);
		return S_OK;
	}

	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CUriRuntimeClass_AddRef(
	IN	IUriRuntimeClass	*This)
{
	return InterlockedIncrement(&This->RefCount);
}

ULONG STDMETHODCALLTYPE CUriRuntimeClass_Release(
	IN	IUriRuntimeClass	*This)
{
	ULONG NewRefCount;

	NewRefCount = InterlockedDecrement(&This->RefCount);

	if (NewRefCount == 0) {
		This->Marshaler->lpVtbl->Release(This->Marshaler);
		This->Uri->lpVtbl->Release(This->Uri);
		SafeFree(This);
	}

	return NewRefCount;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_GetIids(
	IN	IUriRuntimeClass	*This,
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
	Array[0] = IID_IUriRuntimeClass;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_GetRuntimeClassName(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*ClassName)
{
	PCWSTR Name = L"Windows.Foundation.Uri";
	ASSERT (ClassName != NULL);
	return WindowsCreateString(Name, (ULONG) wcslen(Name), ClassName);
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_GetTrustLevel(
	IN	IUriRuntimeClass	*This,
	OUT	TrustLevel			*Level)
{
	ASSERT (Level != NULL);
	*Level = BaseTrust;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_AbsoluteUri(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*AbsoluteUri)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetAbsoluteUri(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), AbsoluteUri);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_DisplayUri(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*DisplayUri)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetDisplayUri(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), DisplayUri);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Domain(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*Domain)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetDomain(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), Domain);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Extension(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*Extension)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetExtension(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), Extension);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Fragment(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*Fragment)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetFragment(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), Fragment);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Host(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*Host)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetHost(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), Host);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Password(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*Password)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetPassword(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), Password);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Path(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*Path)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetPath(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), Path);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Query(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*Query)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetQuery(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), Query);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_QueryParsed(
	IN	IUriRuntimeClass	*This,
	OUT	IUnknown			**QueryParsed)
{
	KexDebugCheckpoint();
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_RawUri(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*RawUri)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetRawUri(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), RawUri);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_SchemeName(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*SchemeName)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetSchemeName(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), SchemeName);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_UserName(
	IN	IUriRuntimeClass	*This,
	OUT	HSTRING				*UserName)
{
	HRESULT Result;
	BSTR Output;

	Result = This->Uri->lpVtbl->GetUserName(This->Uri, &Output);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsCreateString(Output, (ULONG) wcslen(Output), UserName);
	SysFreeString(Output);

	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Port(
	IN	IUriRuntimeClass	*This,
	OUT	PULONG				Port)
{
	return This->Uri->lpVtbl->GetPort(This->Uri, Port);
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_get_Suspicious(
	IN	IUriRuntimeClass	*This,
	OUT	PBOOLEAN			Suspicious)
{
	KexDebugCheckpoint();

	*Suspicious = FALSE;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_Equals(
	IN	IUriRuntimeClass	*This,
	IN	IUriRuntimeClass	*OtherUri,
	OUT	PBOOLEAN			IsEqual)
{
	HRESULT Result;
	BOOL Equal;
	HSTRING OtherText;
	IUri *OtherNativeUri;

	if (!IsEqual) return E_POINTER;
	*IsEqual = FALSE;
	if (!OtherUri) return E_INVALIDARG;
	Result = OtherUri->lpVtbl->get_AbsoluteUri(OtherUri, &OtherText);
	if (FAILED(Result)) return Result;
	Result = KxCompValidateUriString(OtherText, FALSE);
	if (FAILED(Result)) { WindowsDeleteString(OtherText); return Result; }
	Result = CreateUri(WindowsGetStringRawBuffer(OtherText, NULL),
		Uri_CREATE_CANONICALIZE, 0, &OtherNativeUri);
	WindowsDeleteString(OtherText);
	if (FAILED(Result)) return Result;
	Result = This->Uri->lpVtbl->IsEqual(This->Uri, OtherNativeUri, &Equal);
	OtherNativeUri->lpVtbl->Release(OtherNativeUri);
	if (SUCCEEDED(Result)) *IsEqual = (BOOLEAN) Equal;
	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClass_CombineUri(
	IN	IUriRuntimeClass	*This,
	IN	HSTRING				ExtraPart,
	OUT	IUriRuntimeClass	**NewUri)
{
	IUriRuntimeClass *Combined;
	HRESULT Result;
	if (!NewUri) return E_POINTER;
	*NewUri = NULL;
	Result = KxCompValidateUriString(ExtraPart, TRUE);
	if (FAILED(Result)) return Result;
	Combined = SafeAlloc(IUriRuntimeClass, 1);
	if (!Combined) return E_OUTOFMEMORY;
	Combined->lpVtbl = This->lpVtbl;
	Combined->RefCount = 1;
	Result = CoInternetCombineUrlEx(This->Uri,
		WindowsGetStringRawBuffer(ExtraPart, NULL), 0, &Combined->Uri, 0);
	if (FAILED(Result)) {
		SafeFree(Combined);
		return Result;
	}
	Result = CoCreateFreeThreadedMarshaler((IUnknown *) Combined, &Combined->Marshaler);
	if (FAILED(Result)) {
		Combined->Uri->lpVtbl->Release(Combined->Uri);
		SafeFree(Combined);
		return Result;
	}
	*NewUri = Combined;
	return S_OK;
}

IUriRuntimeClassVtbl CUriRuntimeClassVtbl = {
	CUriRuntimeClass_QueryInterface,
	CUriRuntimeClass_AddRef,
	CUriRuntimeClass_Release,
	CUriRuntimeClass_GetIids,
	CUriRuntimeClass_GetRuntimeClassName,
	CUriRuntimeClass_GetTrustLevel,

	CUriRuntimeClass_get_AbsoluteUri,
	CUriRuntimeClass_get_DisplayUri,
	CUriRuntimeClass_get_Domain,
	CUriRuntimeClass_get_Extension,
	CUriRuntimeClass_get_Fragment,
	CUriRuntimeClass_get_Host,
	CUriRuntimeClass_get_Password,
	CUriRuntimeClass_get_Path,
	CUriRuntimeClass_get_Query,
	CUriRuntimeClass_get_QueryParsed,
	CUriRuntimeClass_get_RawUri,
	CUriRuntimeClass_get_SchemeName,
	CUriRuntimeClass_get_UserName,
	CUriRuntimeClass_get_Port,
	CUriRuntimeClass_get_Suspicious,
	CUriRuntimeClass_Equals,
	CUriRuntimeClass_CombineUri
};

HRESULT STDMETHODCALLTYPE CUriRuntimeClassFactory_QueryInterface(
	IN	IUriRuntimeClassFactory	*This,
	IN	REFIID					RefIID,
	OUT	PPVOID					Interface)
{
	UNREFERENCED_PARAMETER(This);
	return KxCompQueryBuiltinFactory(KxRuntimeUri, RefIID, Interface);
}

ULONG STDMETHODCALLTYPE CUriRuntimeClassFactory_AddRef(
	IN	IUriRuntimeClassFactory	*This)
{
	return 1;
}

ULONG STDMETHODCALLTYPE CUriRuntimeClassFactory_Release(
	IN	IUriRuntimeClassFactory	*This)
{
	return 1;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClassFactory_GetIids(
	IN	IUriRuntimeClassFactory	*This,
	OUT	PULONG					NumberOfIids,
	OUT	IID						**IidArray)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeUri, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetIids(Factory, NumberOfIids, IidArray);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClassFactory_GetRuntimeClassName(
	IN	IUriRuntimeClassFactory	*This,
	OUT	HSTRING					*ClassName)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeUri, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetRuntimeClassName(Factory, ClassName);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClassFactory_GetTrustLevel(
	IN	IUriRuntimeClassFactory	*This,
	OUT	TrustLevel				*Level)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeUri, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetTrustLevel(Factory, Level);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClassFactory_CreateUri(
	IN	IUriRuntimeClassFactory	*This,
	IN	HSTRING					Uri,
	OUT	IUriRuntimeClass		**UriRuntimeClass)
{
	HRESULT Result;
	IUriRuntimeClass *RuntimeClass;

	if (!UriRuntimeClass) return E_POINTER;
	*UriRuntimeClass = NULL;
	Result = KxCompValidateUriString(Uri, FALSE);
	if (FAILED(Result)) return Result;

	RuntimeClass = SafeAlloc(IUriRuntimeClass, 1);
	if (!RuntimeClass) {
		return E_OUTOFMEMORY;
	}

	RuntimeClass->lpVtbl = &CUriRuntimeClassVtbl;
	RuntimeClass->RefCount = 1;

	Result = CreateUri(
		WindowsGetStringRawBuffer(Uri, NULL),
		Uri_CREATE_NO_IE_SETTINGS | Uri_CREATE_PRE_PROCESS_HTML_URI |
		Uri_CREATE_CRACK_UNKNOWN_SCHEMES | Uri_CREATE_CANONICALIZE |
		Uri_CREATE_NO_DECODE_EXTRA_INFO | Uri_CREATE_ALLOW_IMPLICIT_FILE_SCHEME,
		0,
		&RuntimeClass->Uri);

	if (FAILED(Result)) {
		KexDebugCheckpoint();
		SafeFree(RuntimeClass);
		return Result;
	}

	ASSERT (RuntimeClass != NULL);
	Result = CoCreateFreeThreadedMarshaler((IUnknown *) RuntimeClass, &RuntimeClass->Marshaler);
	if (FAILED(Result)) {
		RuntimeClass->Uri->lpVtbl->Release(RuntimeClass->Uri);
		SafeFree(RuntimeClass);
		return Result;
	}
	*UriRuntimeClass = RuntimeClass;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CUriRuntimeClassFactory_CreateWithRelativeUri(
	IN	IUriRuntimeClassFactory	*This,
	IN	HSTRING					BaseUri,
	IN	HSTRING					RelativeUri,
	OUT	IUriRuntimeClass		**UriRuntimeClass)
{
	IUriRuntimeClass *Base;
	HRESULT Result;
	if (!UriRuntimeClass) return E_POINTER;
	*UriRuntimeClass = NULL;
	Result = CUriRuntimeClassFactory_CreateUri(This, BaseUri, &Base);
	if (FAILED(Result)) return Result;
	Result = Base->lpVtbl->CombineUri(Base, RelativeUri, UriRuntimeClass);
	Base->lpVtbl->Release(Base);
	return Result;
}

IUriRuntimeClassFactoryVtbl CUriRuntimeClassFactoryVtbl = {
	CUriRuntimeClassFactory_QueryInterface,
	CUriRuntimeClassFactory_AddRef,
	CUriRuntimeClassFactory_Release,
	CUriRuntimeClassFactory_GetIids,
	CUriRuntimeClassFactory_GetRuntimeClassName,
	CUriRuntimeClassFactory_GetTrustLevel,
	CUriRuntimeClassFactory_CreateUri,
	CUriRuntimeClassFactory_CreateWithRelativeUri
};

IUriRuntimeClassFactory CUriRuntimeClassFactory = {
	&CUriRuntimeClassFactoryVtbl
};
