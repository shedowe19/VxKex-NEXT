#include "buildcfg.h"
#include "kxcomp.h"
#include <ObjIdl.h>

// The Shell IAsyncOperation in the Windows 7 headers is NOT the WinRT ABI.
// These private interfaces follow the SDK's Boolean specialization and
// AsyncInfo.h; that Shell type is used only as an opaque factory output.
typedef struct _KX_ASYNC_BOOLEAN KX_ASYNC_BOOLEAN;
typedef struct _KX_ASYNC_INTERFACE KX_ASYNC_INTERFACE;
typedef struct _KX_ASYNC_COMPLETED KX_ASYNC_COMPLETED;

#define KX_ASYNC_INSPECTABLE_METHODS \
	HRESULT (STDMETHODCALLTYPE *QueryInterface)(KX_ASYNC_INTERFACE *, REFIID, PPVOID); \
	ULONG (STDMETHODCALLTYPE *AddRef)(KX_ASYNC_INTERFACE *); \
	ULONG (STDMETHODCALLTYPE *Release)(KX_ASYNC_INTERFACE *); \
	HRESULT (STDMETHODCALLTYPE *GetIids)(KX_ASYNC_INTERFACE *, PULONG, IID **); \
	HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(KX_ASYNC_INTERFACE *, HSTRING *); \
	HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(KX_ASYNC_INTERFACE *, TrustLevel *)

typedef struct _KX_ASYNC_OPERATION_VTBL {
	KX_ASYNC_INSPECTABLE_METHODS;
	HRESULT (STDMETHODCALLTYPE *put_Completed)(KX_ASYNC_INTERFACE *, KX_ASYNC_COMPLETED *);
	HRESULT (STDMETHODCALLTYPE *get_Completed)(KX_ASYNC_INTERFACE *, KX_ASYNC_COMPLETED **);
	HRESULT (STDMETHODCALLTYPE *GetResults)(KX_ASYNC_INTERFACE *, BOOLEAN *);
} KX_ASYNC_OPERATION_VTBL;

typedef struct _KX_ASYNC_INFO_VTBL {
	KX_ASYNC_INSPECTABLE_METHODS;
	HRESULT (STDMETHODCALLTYPE *get_Id)(KX_ASYNC_INTERFACE *, PULONG);
	HRESULT (STDMETHODCALLTYPE *get_Status)(KX_ASYNC_INTERFACE *, LONG *);
	HRESULT (STDMETHODCALLTYPE *get_ErrorCode)(KX_ASYNC_INTERFACE *, HRESULT *);
	HRESULT (STDMETHODCALLTYPE *Cancel)(KX_ASYNC_INTERFACE *);
	HRESULT (STDMETHODCALLTYPE *Close)(KX_ASYNC_INTERFACE *);
} KX_ASYNC_INFO_VTBL;

typedef struct _KX_ASYNC_COMPLETED_VTBL {
	HRESULT (STDMETHODCALLTYPE *QueryInterface)(KX_ASYNC_COMPLETED *, REFIID, PPVOID);
	ULONG (STDMETHODCALLTYPE *AddRef)(KX_ASYNC_COMPLETED *);
	ULONG (STDMETHODCALLTYPE *Release)(KX_ASYNC_COMPLETED *);
	HRESULT (STDMETHODCALLTYPE *Invoke)(KX_ASYNC_COMPLETED *, KX_ASYNC_INTERFACE *, LONG);
} KX_ASYNC_COMPLETED_VTBL;

struct _KX_ASYNC_COMPLETED { const KX_ASYNC_COMPLETED_VTBL *lpVtbl; };
struct _KX_ASYNC_INTERFACE { const VOID *lpVtbl; KX_ASYNC_BOOLEAN *Owner; };
struct _KX_ASYNC_BOOLEAN {
	KX_ASYNC_INTERFACE Operation;
	KX_ASYNC_INTERFACE Info;
	volatile LONG References;
	volatile LONG Closed;
	BOOLEAN Value;
	BOOLEAN Assigned;
	ULONG Id;
	DWORD CompletedCookie;
	DWORD RetiredCookie;
	ULONG ActiveLookups;
	CRITICAL_SECTION Lock;
	IUnknown *Marshaler;
	IGlobalInterfaceTable *Git;
};

static const IID KxAsyncBooleanIid = {0xcdb5efb3,0x5788,0x509d,{0x9b,0xe1,0x71,0xcc,0xb8,0xa3,0x36,0x2a}};
static const IID KxAsyncCompletedIid = {0xc1d3d1a2,0xae17,0x5a5f,{0xb5,0xa2,0xbd,0xcc,0x88,0x44,0x88,0x9a}};
static const IID KxAsyncInfoIid = {0x00000036,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID KxAsyncMarshalIid = {0x00000003,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID KxAsyncGitIid = {0x00000146,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const CLSID KxAsyncGitClsid = {0x00000323,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static volatile LONG KxAsyncNextId;

#define KX_E_ILLEGAL_METHOD_CALL ((HRESULT) 0x8000000eL)
#define KX_E_ILLEGAL_DELEGATE_ASSIGNMENT ((HRESULT) 0x80000018L)

static ULONG STDMETHODCALLTYPE KxAsyncAddRef(KX_ASYNC_INTERFACE *This)
{
	return InterlockedIncrement(&This->Owner->References);
}

static ULONG STDMETHODCALLTYPE KxAsyncRelease(KX_ASYNC_INTERFACE *This)
{
	KX_ASYNC_BOOLEAN *Object = This->Owner;
	ULONG References = InterlockedDecrement(&Object->References);
	if (!References) {
		if (Object->CompletedCookie) {
			Object->Git->lpVtbl->RevokeInterfaceFromGlobal(Object->Git, Object->CompletedCookie);
		}
		if (Object->RetiredCookie) {
			Object->Git->lpVtbl->RevokeInterfaceFromGlobal(Object->Git, Object->RetiredCookie);
		}
		Object->Git->lpVtbl->Release(Object->Git);
		Object->Marshaler->lpVtbl->Release(Object->Marshaler);
		DeleteCriticalSection(&Object->Lock);
		CoTaskMemFree(Object);
	}
	return References;
}

static HRESULT STDMETHODCALLTYPE KxAsyncQueryInterface(KX_ASYNC_INTERFACE *This, REFIID Iid, PPVOID Result)
{
	KX_ASYNC_BOOLEAN *Object = This->Owner;
	if (!Result) return E_POINTER;
	*Result = NULL;
	if (!Iid) return E_POINTER;
	if (IsEqualIID(Iid, &IID_IUnknown) || IsEqualIID(Iid, &IID_IInspectable) ||
		IsEqualIID(Iid, &KxAsyncBooleanIid) || IsEqualIID(Iid, &IID_IAgileObject)) {
		*Result = &Object->Operation;
	} else if (IsEqualIID(Iid, &KxAsyncInfoIid)) {
		*Result = &Object->Info;
	} else if (IsEqualIID(Iid, &KxAsyncMarshalIid)) {
		return Object->Marshaler->lpVtbl->QueryInterface(Object->Marshaler, Iid, Result);
	} else {
		return E_NOINTERFACE;
	}
	KxAsyncAddRef(This);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetIids(KX_ASYNC_INTERFACE *This, PULONG Count, IID **Iids)
{
	IID *Buffer;
	UNREFERENCED_PARAMETER(This);
	if (Count) *Count = 0;
	if (Iids) *Iids = NULL;
	if (!Count || !Iids) return E_POINTER;
	Buffer = CoTaskMemAlloc(2 * sizeof(IID));
	if (!Buffer) return E_OUTOFMEMORY;
	Buffer[0] = KxAsyncBooleanIid;
	Buffer[1] = KxAsyncInfoIid;
	*Count = 2;
	*Iids = Buffer;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetRuntimeClassName(KX_ASYNC_INTERFACE *This, HSTRING *Name)
{
	static const WCHAR ClassName[] = L"Windows.Foundation.IAsyncOperation`1<Boolean>";
	UNREFERENCED_PARAMETER(This);
	if (!Name) return E_POINTER;
	*Name = NULL;
	return WindowsCreateString(ClassName, ARRAYSIZE(ClassName) - 1, Name);
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetTrustLevel(KX_ASYNC_INTERFACE *This, TrustLevel *Level)
{
	UNREFERENCED_PARAMETER(This);
	if (!Level) return E_POINTER;
	*Level = BaseTrust;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetResults(KX_ASYNC_INTERFACE *This, BOOLEAN *Value)
{
	if (!Value) return E_POINTER;
	*Value = FALSE;
	if (InterlockedCompareExchange(&This->Owner->Closed, 0, 0)) return KX_E_ILLEGAL_METHOD_CALL;
	*Value = This->Owner->Value;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetCompleted(KX_ASYNC_INTERFACE *This, KX_ASYNC_COMPLETED **Handler)
{
	KX_ASYNC_BOOLEAN *Object = This->Owner;
	IUnknown *Unknown = NULL;
	DWORD Cookie;
	DWORD RevokeCookie = 0;
	BOOL Lookup = FALSE;
	HRESULT Result;
	if (!Handler) return E_POINTER;
	*Handler = NULL;
	KxAsyncAddRef(This);
	EnterCriticalSection(&Object->Lock);
	Cookie = Object->CompletedCookie;
	Result = Object->Closed ? KX_E_ILLEGAL_METHOD_CALL : S_OK;
	if (SUCCEEDED(Result) && Cookie) {
		++Object->ActiveLookups;
		Lookup = TRUE;
	}
	LeaveCriticalSection(&Object->Lock);
	__try {
		if (Lookup) {
			// Resolve for the calling apartment; do not retain a raw STA delegate.
			// GIT requires Get and Revoke for the same cookie to be coordinated.
			// The lookup pin defers revocation without holding Lock over a COM
			// call, which can pump messages and reenter Close on this thread.
			Result = Object->Git->lpVtbl->GetInterfaceFromGlobal(Object->Git, Cookie, &IID_IUnknown, (PPVOID) &Unknown);
			if (SUCCEEDED(Result)) {
				Result = Unknown->lpVtbl->QueryInterface(Unknown, &KxAsyncCompletedIid, (PPVOID) Handler);
			}
		}
	} __finally {
		__try {
			if (Unknown) Unknown->lpVtbl->Release(Unknown);
		} __finally {
			EnterCriticalSection(&Object->Lock);
			if (Lookup && --Object->ActiveLookups == 0) {
				RevokeCookie = Object->RetiredCookie;
				Object->RetiredCookie = 0;
			}
			LeaveCriticalSection(&Object->Lock);
			__try {
				if (RevokeCookie) Object->Git->lpVtbl->RevokeInterfaceFromGlobal(Object->Git, RevokeCookie);
			} __finally {
				KxAsyncRelease(This);
			}
		}
	}
	return Result;
}

// Lock must be held. Assignment is write-once, so at most one cookie can be
// retired. Each active lookup also owns an operation reference; the last one
// revokes outside Lock before releasing that reference. Never wait in Close.
static DWORD KxAsyncDetachCompletedCookieLocked(KX_ASYNC_BOOLEAN *Object)
{
	DWORD Cookie = Object->CompletedCookie;
	Object->CompletedCookie = 0;
	if (Cookie && Object->ActiveLookups) {
		Object->RetiredCookie = Cookie;
		Cookie = 0;
	}
	return Cookie;
}

static HRESULT STDMETHODCALLTYPE KxAsyncPutCompleted(KX_ASYNC_INTERFACE *This, KX_ASYNC_COMPLETED *Handler)
{
	KX_ASYNC_BOOLEAN *Object = This->Owner;
	DWORD Cookie = 0;
	DWORD RevokeCookie = 0;
	HRESULT Result;
	KxAsyncAddRef(This); // The callback may release its caller's last reference.
	EnterCriticalSection(&Object->Lock);
	if (Object->Closed) Result = KX_E_ILLEGAL_METHOD_CALL;
	else if (Object->Assigned) Result = KX_E_ILLEGAL_DELEGATE_ASSIGNMENT;
	else { Object->Assigned = TRUE; Result = S_OK; }
	LeaveCriticalSection(&Object->Lock);
	if (FAILED(Result) || !Handler) {
		KxAsyncRelease(This);
		return Result;
	}

	Handler->lpVtbl->AddRef(Handler);
	Result = Object->Git->lpVtbl->RegisterInterfaceInGlobal(Object->Git, (IUnknown *) Handler, &IID_IUnknown, &Cookie);
	EnterCriticalSection(&Object->Lock);
	if (FAILED(Result)) Object->Assigned = FALSE;
	else if (Object->Closed) Result = KX_E_ILLEGAL_METHOD_CALL;
	else { Object->CompletedCookie = Cookie; Cookie = 0; }
	LeaveCriticalSection(&Object->Lock);
	if (Cookie) Object->Git->lpVtbl->RevokeInterfaceFromGlobal(Object->Git, Cookie);
	if (SUCCEEDED(Result)) {
		// This operation is already Completed (AsyncStatus == 1). Invoke
		// synchronously, exactly once, with no lock held. Like WRL AsyncBase,
		// an HRESULT returned by the notification does not change the result.
		__try {
			Handler->lpVtbl->Invoke(Handler, &Object->Operation, 1);
		} __finally {
			EnterCriticalSection(&Object->Lock);
			RevokeCookie = KxAsyncDetachCompletedCookieLocked(Object);
			LeaveCriticalSection(&Object->Lock);
			if (RevokeCookie) Object->Git->lpVtbl->RevokeInterfaceFromGlobal(Object->Git, RevokeCookie);
			Handler->lpVtbl->Release(Handler);
			KxAsyncRelease(This);
		}
		return S_OK;
	}
	Handler->lpVtbl->Release(Handler);
	KxAsyncRelease(This);
	return Result;
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetId(KX_ASYNC_INTERFACE *This, PULONG Id)
{
	if (!Id) return E_POINTER;
	*Id = This->Owner->Id;
	return InterlockedCompareExchange(&This->Owner->Closed, 0, 0) ? KX_E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetStatus(KX_ASYNC_INTERFACE *This, LONG *Status)
{
	LONG Closed;
	if (!Status) return E_POINTER;
	Closed = InterlockedCompareExchange(&This->Owner->Closed, 0, 0);
	// Native completed operations expose the internal Closed value (4) along
	// with E_ILLEGAL_METHOD_CALL; it is not a successful AsyncStatus result.
	*Status = Closed ? 4 : 1;
	return Closed ? KX_E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT STDMETHODCALLTYPE KxAsyncGetErrorCode(KX_ASYNC_INTERFACE *This, HRESULT *Error)
{
	if (!Error) return E_POINTER;
	*Error = InterlockedCompareExchange(&This->Owner->Closed, 0, 0) ? KX_E_ILLEGAL_METHOD_CALL : S_OK;
	return *Error;
}

static HRESULT STDMETHODCALLTYPE KxAsyncCancel(KX_ASYNC_INTERFACE *This)
{
	UNREFERENCED_PARAMETER(This);
	// Cancellation cannot change an already completed operation.
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE KxAsyncClose(KX_ASYNC_INTERFACE *This)
{
	KX_ASYNC_BOOLEAN *Object = This->Owner;
	DWORD Cookie;
	KxAsyncAddRef(This);
	EnterCriticalSection(&Object->Lock);
	InterlockedExchange(&Object->Closed, TRUE);
	Cookie = KxAsyncDetachCompletedCookieLocked(Object);
	LeaveCriticalSection(&Object->Lock);
	if (Cookie) Object->Git->lpVtbl->RevokeInterfaceFromGlobal(Object->Git, Cookie);
	KxAsyncRelease(This);
	return S_OK;
}

#define KX_ASYNC_COMMON_VTABLE KxAsyncQueryInterface, KxAsyncAddRef, KxAsyncRelease, KxAsyncGetIids, KxAsyncGetRuntimeClassName, KxAsyncGetTrustLevel
static const KX_ASYNC_OPERATION_VTBL KxAsyncOperationVtbl = {
	KX_ASYNC_COMMON_VTABLE, KxAsyncPutCompleted, KxAsyncGetCompleted, KxAsyncGetResults
};
static const KX_ASYNC_INFO_VTBL KxAsyncInfoVtbl = {
	KX_ASYNC_COMMON_VTABLE, KxAsyncGetId, KxAsyncGetStatus, KxAsyncGetErrorCode, KxAsyncCancel, KxAsyncClose
};

HRESULT KxCompCreateCompletedBooleanOperation(BOOLEAN Value, IAsyncOperation **Operation)
{
	KX_ASYNC_BOOLEAN *Object;
	HRESULT Result;
	if (!Operation) return E_POINTER;
	*Operation = NULL;
	Object = CoTaskMemAlloc(sizeof(*Object));
	if (!Object) return E_OUTOFMEMORY;
	RtlZeroMemory(Object, sizeof(*Object));
	if (!InitializeCriticalSectionAndSpinCount(&Object->Lock, 0)) {
		CoTaskMemFree(Object);
		return E_OUTOFMEMORY;
	}
	Object->Operation.lpVtbl = &KxAsyncOperationVtbl;
	Object->Operation.Owner = Object;
	Object->Info.lpVtbl = &KxAsyncInfoVtbl;
	Object->Info.Owner = Object;
	Object->References = 1;
	Object->Value = Value ? TRUE : FALSE;
	do { Object->Id = (ULONG) InterlockedIncrement(&KxAsyncNextId); } while (!Object->Id);
	Result = CoCreateFreeThreadedMarshaler((IUnknown *) &Object->Operation, &Object->Marshaler);
	if (SUCCEEDED(Result)) {
		Result = CoCreateInstance(&KxAsyncGitClsid, NULL, CLSCTX_INPROC_SERVER,
			&KxAsyncGitIid, (PPVOID) &Object->Git);
	}
	if (FAILED(Result)) {
		if (Object->Marshaler) Object->Marshaler->lpVtbl->Release(Object->Marshaler);
		DeleteCriticalSection(&Object->Lock);
		CoTaskMemFree(Object);
		return Result;
	}
	*Operation = (IAsyncOperation *) &Object->Operation;
	return S_OK;
}
