#include "buildcfg.h"
#include "kxcomp.h"
#include <ShellAPI.h>

HRESULT STDMETHODCALLTYPE CLauncherStatics_QueryInterface(
	IN	ILauncherStatics	*This,
	IN	REFIID				RefIID,
	OUT	PPVOID				Interface)
{
	UNREFERENCED_PARAMETER(This);
	return KxCompQueryBuiltinFactory(KxRuntimeLauncher, RefIID, Interface);
}

ULONG STDMETHODCALLTYPE CLauncherStatics_AddRef(
	IN	ILauncherStatics	*This)
{
	return 1;
}

ULONG STDMETHODCALLTYPE CLauncherStatics_Release(
	IN	ILauncherStatics	*This)
{
	return 1;
}

HRESULT STDMETHODCALLTYPE CLauncherStatics_GetIids(
	IN	ILauncherStatics	*This,
	OUT	PULONG				NumberOfIids,
	OUT	IID					**IidArray)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeLauncher, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetIids(Factory, NumberOfIids, IidArray);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

HRESULT STDMETHODCALLTYPE CLauncherStatics_GetRuntimeClassName(
	IN	ILauncherStatics	*This,
	OUT	HSTRING				*ClassName)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeLauncher, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetRuntimeClassName(Factory, ClassName);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

HRESULT STDMETHODCALLTYPE CLauncherStatics_GetTrustLevel(
	IN	ILauncherStatics	*This,
	OUT	TrustLevel			*Level)
{
	IActivationFactory *Factory;
	HRESULT Result;
	UNREFERENCED_PARAMETER(This);
	Result = KxCompQueryBuiltinFactory(KxRuntimeLauncher, &IID_IActivationFactory, (PPVOID) &Factory);
	if (FAILED(Result)) return Result;
	Result = Factory->lpVtbl->GetTrustLevel(Factory, Level);
	Factory->lpVtbl->Release(Factory);
	return Result;
}

HRESULT STDMETHODCALLTYPE CLauncherStatics_LaunchFileAsync(
	IN	ILauncherStatics	*This,
	IN	IUnknown			*StorageFile,
	OUT	IAsyncOperation		**AsyncOperation)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(StorageFile);
	if (!AsyncOperation) return E_POINTER;
	*AsyncOperation = NULL;
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CLauncherStatics_LaunchFileWithOptionsAsync(
	IN	ILauncherStatics	*This,
	IN	IUnknown			*StorageFile,
	IN	IUnknown			*LauncherOptions,
	OUT	IAsyncOperation		**AsyncOperation)
{
	KexDebugCheckpoint(); // not properly implemented
	return CLauncherStatics_LaunchFileAsync(This, StorageFile, AsyncOperation);
}

HRESULT STDMETHODCALLTYPE CLauncherStatics_LaunchUriAsync(
	IN	ILauncherStatics	*This,
	IN	IUriRuntimeClass	*Uri,
	OUT	IAsyncOperation		**AsyncOperation)
{
	HRESULT Result;
	HSTRING RawUri;
	HSTRING Scheme;
	BOOL EmbeddedNull;
	BOOLEAN Launched;
	SHELLEXECUTEINFOW Execute;
	UNREFERENCED_PARAMETER(This);
	if (!AsyncOperation) return E_POINTER;
	*AsyncOperation = NULL;
	if (!Uri) return E_POINTER;

	Result = Uri->lpVtbl->get_AbsoluteUri(Uri, &RawUri);
	if (FAILED(Result)) {
		return Result;
	}

	Result = WindowsStringHasEmbeddedNull(RawUri, &EmbeddedNull);
	if (FAILED(Result) || EmbeddedNull || WindowsIsStringEmpty(RawUri)) {
		WindowsDeleteString(RawUri);
		return FAILED(Result) ? Result : E_INVALIDARG;
	}
	Result = Uri->lpVtbl->get_SchemeName(Uri, &Scheme);
	if (FAILED(Result)) {
		WindowsDeleteString(RawUri);
		return Result;
	}
	// LaunchUriAsync does not provide the LaunchFileAsync contract.
	if (WindowsIsStringEmpty(Scheme) ||
		CompareStringOrdinal(WindowsGetStringRawBuffer(Scheme, NULL), -1, L"file", -1, TRUE) == CSTR_EQUAL) {
		WindowsDeleteString(Scheme);
		WindowsDeleteString(RawUri);
		return KxCompCreateCompletedBooleanOperation(FALSE, AsyncOperation);
	}
	WindowsDeleteString(Scheme);
	ZeroMemory(&Execute, sizeof(Execute));
	Execute.cbSize = sizeof(Execute);
	Execute.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
	Execute.lpVerb = L"open";
	Execute.lpFile = WindowsGetStringRawBuffer(RawUri, NULL);
	Execute.nShow = SW_SHOWNORMAL;
	Launched = (BOOLEAN) !!ShellExecuteExW(&Execute);
	WindowsDeleteString(RawUri);
	return KxCompCreateCompletedBooleanOperation(Launched, AsyncOperation);
}

HRESULT STDMETHODCALLTYPE CLauncherStatics_LaunchUriWithOptionsAsync(
	IN	ILauncherStatics	*This,
	IN	IUriRuntimeClass	*Uri,
	IN	IUnknown			*LauncherOptions,
	OUT	IAsyncOperation		**AsyncOperation)
{
	if (!AsyncOperation) return E_POINTER;
	*AsyncOperation = NULL;
	if (LauncherOptions) return E_NOTIMPL;
	return CLauncherStatics_LaunchUriAsync(This, Uri, AsyncOperation);
}

ILauncherStaticsVtbl CLauncherStaticsVtbl = {
	CLauncherStatics_QueryInterface,
	CLauncherStatics_AddRef,
	CLauncherStatics_Release,
	CLauncherStatics_GetIids,
	CLauncherStatics_GetRuntimeClassName,
	CLauncherStatics_GetTrustLevel,
	CLauncherStatics_LaunchFileAsync,
	CLauncherStatics_LaunchFileWithOptionsAsync,
	CLauncherStatics_LaunchUriAsync,
	CLauncherStatics_LaunchUriWithOptionsAsync
};

ILauncherStatics CLauncherStatics = {
	&CLauncherStaticsVtbl
};
