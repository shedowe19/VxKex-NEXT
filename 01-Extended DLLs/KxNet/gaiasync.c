///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     gaiasync.c
//
// Abstract:
//
//     Implements asynchronous GetAddrInfoExW.
//
// Author:
//
//     vxiiduu (30-Jun-2026)
//
// Revision History:
//
//     vxiiduu              30-Jun-2026  Initial creation.
//
///////////////////////////////////////////////////////////////////////////////

#include "buildcfg.h"
#include "kxnetp.h"

//
// Data Types used for this feature.
//

typedef enum {
	GaiWaiting = 0,
	GaiCompletionInProgress,
	GaiCompleted
} TYPEDEF_TYPE_NAME(KXNETP_GAI_STATE);

typedef struct _KXNETP_GAI_ASYNC_CONTEXT *PKXNETP_GAI_ASYNC_CONTEXT;

typedef struct _KXNETP_GAI_ASYNC_CONTEXT {
	VOLATILE LONG					References;
	VOLATILE KXNETP_GAI_STATE			State;

	// Protected by GaiHandleLock. CancelHandle is an opaque token, never a
	// caller-supplied address which GetAddrInfoExCancel may dereference.
	PKXNETP_GAI_ASYNC_CONTEXT			Next;
	HANDLE								CancelHandle;

	union {
		struct {
			BOOLEAN						NamePresent:1;
			BOOLEAN						ServiceNamePresent:1;
			BOOLEAN						NspGuidPresent:1;
			BOOLEAN						HintsPresent:1;
			BOOLEAN						TimeoutPresent:1;
		};

		UCHAR							Flags;
	};

	// Copies of input data must outlive Ext_GetAddrInfoExW's stack.
	WCHAR								Name[256];
	WCHAR								ServiceName[256];
	ULONG								Namespace;
	GUID								NspGuid;
	ADDRINFOEXW							Hints;
	PPADDRINFOEXW						AddrInfoResult;
	TIMEVAL								Timeout;
	LPOVERLAPPED						Overlapped;
	LPLOOKUPSERVICE_COMPLETION_ROUTINE	CompletionRoutine OPTIONAL;
	HANDLE								CompletionEvent OPTIONAL;

	// Created before submission, armed and closed only by the worker. The
	// worker owns a reference until every timer callback has returned.
	PTP_TIMER							TimeoutTimer OPTIONAL;
} TYPEDEF_TYPE_NAME(KXNETP_GAI_ASYNC_CONTEXT);

STATIC SRWLOCK GaiHandleLock = SRWLOCK_INIT;
STATIC PKXNETP_GAI_ASYNC_CONTEXT GaiHandleList;
STATIC ULONG_PTR GaiNextHandle;

STATIC VOID GetAddrInfoExReferenceAsyncContext(
	IN OUT	PKXNETP_GAI_ASYNC_CONTEXT AsyncContext)
{
	InterlockedIncrement(&AsyncContext->References);
}

STATIC VOID GetAddrInfoExDereferenceAsyncContext(
	IN OUT	PKXNETP_GAI_ASYNC_CONTEXT AsyncContext)
{
	if (InterlockedDecrement(&AsyncContext->References) == 0) {
		ASSERT (AsyncContext->TimeoutTimer == NULL);
		HeapFree(GetProcessHeap(), 0, AsyncContext);
	}
}

STATIC BOOLEAN GetAddrInfoExRegisterAsyncOperation(
	IN OUT	PKXNETP_GAI_ASYNC_CONTEXT AsyncContext)
{
	// The submitting thread already holds GaiHandleLock.
	// Never recycle a token, even after its context has been freed. A stale
	// cancel handle must not accidentally cancel a later request (ABA).
	// Reserve NULL and INVALID_HANDLE_VALUE. On exhaustion, fail the request.
	if (GaiNextHandle == (ULONG_PTR) -2) {
		return FALSE;
	}

	AsyncContext->CancelHandle = (HANDLE) ++GaiNextHandle;
	AsyncContext->Next = GaiHandleList;
	GaiHandleList = AsyncContext;

	return TRUE;
}

STATIC VOID GetAddrInfoExUnregisterAsyncOperation(
	IN OUT	PKXNETP_GAI_ASYNC_CONTEXT AsyncContext)
{
	PKXNETP_GAI_ASYNC_CONTEXT *Link;

	AcquireSRWLockExclusive(&GaiHandleLock);

	for (Link = &GaiHandleList; *Link; Link = &(*Link)->Next) {
		if (*Link == AsyncContext) {
			*Link = AsyncContext->Next;
			AsyncContext->Next = NULL;
			break;
		}
	}

	ReleaseSRWLockExclusive(&GaiHandleLock);
}

STATIC PKXNETP_GAI_ASYNC_CONTEXT GetAddrInfoExLookupAsyncOperation(
	IN	HANDLE CancelHandle)
{
	PKXNETP_GAI_ASYNC_CONTEXT AsyncContext;

	AcquireSRWLockExclusive(&GaiHandleLock);

	for (AsyncContext = GaiHandleList; AsyncContext; AsyncContext = AsyncContext->Next) {
		if (AsyncContext->CancelHandle == CancelHandle) {
			// Take the reference while removal/freeing is excluded by the lock.
			GetAddrInfoExReferenceAsyncContext(AsyncContext);
			break;
		}
	}

	ReleaseSRWLockExclusive(&GaiHandleLock);
	return AsyncContext;
}

// The caller of this helper must own a reference. Only the winner may touch
// caller-owned output buffers, and it does so before notifying completion.
STATIC BOOLEAN GetAddrInfoExCompleteAsyncOperation(
	IN OUT	PKXNETP_GAI_ASYNC_CONTEXT	AsyncContext,
	IN		PADDRINFOEXW				AddrInfoResult OPTIONAL,
	IN		INT							GaiReturnCode)
{
	BOOL Success;
	KXNETP_GAI_STATE OldState;

	OldState = (KXNETP_GAI_STATE) InterlockedCompareExchange(
		(VOLATILE LONG *) &AsyncContext->State,
		GaiCompletionInProgress,
		GaiWaiting);

	if (OldState != GaiWaiting) {
		return FALSE;
	}

	// Remove before invoking user code. Reentrant cancellation and late
	// cancellation then fail without looking at a completed context.
	GetAddrInfoExUnregisterAsyncOperation(AsyncContext);

	*AsyncContext->AddrInfoResult = AddrInfoResult;
	AsyncContext->Overlapped->Internal = GaiReturnCode;
	AsyncContext->Overlapped->Pointer = AsyncContext->AddrInfoResult;

	if (AsyncContext->CompletionRoutine) {
		AsyncContext->CompletionRoutine(
			GaiReturnCode,
			0,
			AsyncContext->Overlapped);
	} else {
		Success = SetEvent(AsyncContext->CompletionEvent);
		ASSERT (Success);
	}

	// The callback may have freed OVERLAPPED and the result pointer. Do not
	// access either after notification; the heap context has its own lifetime.
	InterlockedExchange((VOLATILE LONG *) &AsyncContext->State, GaiCompleted);

	KexLogDebugEvent(
		L"Asynchronous GetAddrInfoEx completed (%s)\r\n\r\n"
		L"AsyncContext:       0x%p\r\n"
		L"AddrInfoResult:     0x%p\r\n"
		L"GaiReturnCode:      %d",
		AsyncContext->Name,
		AsyncContext,
		AddrInfoResult,
		GaiReturnCode);

	return TRUE;
}

STATIC VOID CALLBACK GetAddrInfoExAsyncTimeoutCallback(
	IN OUT	PTP_CALLBACK_INSTANCE	Instance,
	IN OUT	PVOID					Parameter,
	IN OUT	PTP_TIMER				Timer)
{
	// The worker holds the context alive until it has disarmed this timer and
	// WaitForThreadpoolTimerCallbacks has waited for this callback to return.
	GetAddrInfoExCompleteAsyncOperation(
		(PKXNETP_GAI_ASYNC_CONTEXT) Parameter,
		NULL,
		WSAETIMEDOUT);
}

STATIC VOID CALLBACK GetAddrInfoExAsyncThreadpoolCallback(
	IN OUT	PTP_CALLBACK_INSTANCE	Instance,
	IN OUT	PVOID					Parameter OPTIONAL)
{
	PKXNETP_GAI_ASYNC_CONTEXT AsyncContext;
	PADDRINFOEXW AddrInfoResult;
	INT GaiReturnCode;
	BOOLEAN Success;

	AsyncContext = (PKXNETP_GAI_ASYNC_CONTEXT) Parameter;

	// Cancellation can win even before this work item starts. In that case
	// neither the resolver nor the timer needs to be started.
	if (InterlockedCompareExchange((VOLATILE LONG *) &AsyncContext->State,
		GaiWaiting, GaiWaiting) == GaiWaiting) {

		if (AsyncContext->TimeoutTimer) {
			LARGE_INTEGER DueTime;

			DueTime.QuadPart = -((LONGLONG) AsyncContext->Timeout.tv_sec * 10000000 +
				(LONGLONG) AsyncContext->Timeout.tv_usec * 10);

			SetThreadpoolTimer(AsyncContext->TimeoutTimer, (PFILETIME) &DueTime, 0, 0);
		}

		AddrInfoResult = NULL;
		GaiReturnCode = GetAddrInfoExW(
			AsyncContext->NamePresent ? AsyncContext->Name : NULL,
			AsyncContext->ServiceNamePresent ? AsyncContext->ServiceName : NULL,
			AsyncContext->Namespace,
			AsyncContext->NspGuidPresent ? &AsyncContext->NspGuid : NULL,
			AsyncContext->HintsPresent ? &AsyncContext->Hints : NULL,
			&AddrInfoResult,
			NULL,
			NULL,
			NULL,
			NULL);

		Success = GetAddrInfoExCompleteAsyncOperation(
			AsyncContext,
			AddrInfoResult,
			GaiReturnCode);

		if (!Success && GaiReturnCode == NO_ERROR) {
			// Cancellation/timeout already completed the request. Discard the
			// late resolver result rather than touching the caller's buffers.
			FreeAddrInfoExW(AddrInfoResult);
		}
	}

	if (AsyncContext->TimeoutTimer) {
		SetThreadpoolTimer(AsyncContext->TimeoutTimer, NULL, 0, 0);
		WaitForThreadpoolTimerCallbacks(AsyncContext->TimeoutTimer, TRUE);
		CloseThreadpoolTimer(AsyncContext->TimeoutTimer);
		AsyncContext->TimeoutTimer = NULL;
	}

	GetAddrInfoExDereferenceAsyncContext(AsyncContext);
}

//
// Added to support osu!lazer.
// Only GetAddrInfoExW needs to be here because asynchronous lookups are not
// supported for the ANSI version of the function (it returns WSAEINVAL on Win10
// if Timeout, Overlapped, CompletionRoutine, or EventHandle is non-NULL).
//

KXNETAPI INT WSAAPI Ext_GetAddrInfoExW(
	IN	PCWSTR								Name OPTIONAL,
	IN	PCWSTR								ServiceName OPTIONAL,
	IN	ULONG								Namespace,
	IN	LPGUID								NspGuid OPTIONAL,
	IN	PCADDRINFOEXW						Hints OPTIONAL,
	OUT	PPADDRINFOEXW						AddrInfoResult,
	IN	PTIMEVAL							Timeout OPTIONAL,
	IN	LPOVERLAPPED						Overlapped OPTIONAL,
	IN	LPLOOKUPSERVICE_COMPLETION_ROUTINE	CompletionRoutine OPTIONAL,
	OUT	PHANDLE								CancelHandle OPTIONAL)
{
	BOOL Success;
	HRESULT Result;
	TP_CALLBACK_ENVIRON CallbackEnvironment;
	KXNETP_GAI_ASYNC_CONTEXT AsyncContext;
	PKXNETP_GAI_ASYNC_CONTEXT HeapContext;

	if (!Timeout && !Overlapped && !CompletionRoutine && !CancelHandle) {
		// Not an asynchronous operation. No need to do anything, just call
		// original function.
		return GetAddrInfoExW(
			Name,
			ServiceName,
			Namespace,
			NspGuid,
			Hints,
			AddrInfoResult,
			NULL,
			NULL,
			NULL,
			NULL);
	}

	//
	// Validate parameters.
	//

	if (CancelHandle) {
		*CancelHandle = NULL;
	}

	if (AddrInfoResult == NULL) {
		SetLastError(WSAEFAULT);
		return WSAEFAULT;
	}

	*AddrInfoResult = NULL;

	if (!Name && !ServiceName) {
		KexDebugCheckpoint();
		SetLastError(WSAHOST_NOT_FOUND);
		return WSAHOST_NOT_FOUND;
	}

	if (!Overlapped) {
		// All asynchronous lookups must have an Overlapped parameter
		KexDebugCheckpoint();
		SetLastError(WSAEINVAL);
		return WSAEINVAL;
	}

	Overlapped->Internal = WSAEINPROGRESS;

	//
	// The completion routine and the hEvent member of OVERLAPPED are mutually
	// exclusive, and one of them must be specified.
	//

	unless (!!CompletionRoutine ^ !!Overlapped->hEvent) {
		KexDebugCheckpoint();
		SetLastError(WSAEINVAL);
		return WSAEINVAL;
	}

	//
	// Validate the contents of the Timeout struct if it is supplied.
	//

	if (Timeout) {
		// This is a great example of why unix using "int" everywhere is a really
		// stupid and bad idea. You have to constantly check whether things are
		// negative. Why not just use ULONG...
		// Anyway, basically, neither of the values can be negative and the total
		// timeout can't be zero.
		if (Timeout->tv_sec < 0 ||
			Timeout->tv_usec < 0 ||
			(Timeout->tv_sec == 0 && Timeout->tv_usec == 0)) {

			KexDebugCheckpoint();
			SetLastError(WSAEINVAL);
			return WSAEINVAL;
		}
	}

	//
	// Validate Hints struct if supplied.
	//

	if (Hints) {
		if (Hints->ai_addrlen || Hints->ai_canonname || Hints->ai_addr || Hints->ai_next) {
			// as documented by MS
			KexDebugCheckpoint();
			SetLastError(WSANO_RECOVERY);
			return WSANO_RECOVERY;
		}

		if (!Name) {
			if ((Hints->ai_flags & AI_CANONNAME) || (Hints->ai_flags & AI_FQDN)) {
				// these flags don't make sense if a name isn't supplied to us
				KexDebugCheckpoint();
				SetLastError(WSAEINVAL);
				return WSAEINVAL;
			}
		}

		switch (Hints->ai_family) {
		case AF_UNSPEC:
		case AF_INET:
		case AF_INET6:
			break;
		default:
			KexDebugCheckpoint();
			SetLastError(WSAEAFNOSUPPORT);
			return WSAEAFNOSUPPORT;
		}
	}

	//
	// The real GetAddrInfoExW does some additional validation but they require
	// lots of code and are not worth the effort.
	//
	// Set up the context for the asynchronous task.
	// Most of the information supplied through pointers must be *copied* into the
	// asynchronous context, since that is what Windows does, and it's very
	// possible that after we return all those pointers become invalid.
	//

	KexRtlZeroMemory(&AsyncContext, sizeof(AsyncContext));
	
	if (Name) {
		Result = StringCchCopy(
			AsyncContext.Name,
			ARRAYSIZE(AsyncContext.Name),
			Name);

		ASSERT (SUCCEEDED(Result));

		if (FAILED(Result)) {
			SetLastError(WSAENAMETOOLONG);
			return WSAENAMETOOLONG;
		}

		AsyncContext.NamePresent = TRUE;
	}

	if (ServiceName) {
		Result = StringCchCopy(
			AsyncContext.ServiceName,
			ARRAYSIZE(AsyncContext.ServiceName),
			ServiceName);

		ASSERT (SUCCEEDED(Result));

		if (FAILED(Result)) {
			SetLastError(WSAENAMETOOLONG);
			return WSAENAMETOOLONG;
		}

		AsyncContext.ServiceNamePresent = TRUE;
	}

	if (NspGuid) {
		AsyncContext.NspGuid = *NspGuid;
		AsyncContext.NspGuidPresent = TRUE;
	}

	if (Hints) {
		AsyncContext.Hints = *Hints;
		AsyncContext.HintsPresent = TRUE;
	}

	if (Timeout) {
		AsyncContext.Timeout = *Timeout;
		AsyncContext.TimeoutPresent = TRUE;
	}

	Overlapped->Pointer				= NULL;

	AsyncContext.Namespace			= Namespace;
	AsyncContext.AddrInfoResult		= AddrInfoResult;
	AsyncContext.Overlapped			= Overlapped;
	AsyncContext.CompletionRoutine	= CompletionRoutine;
	AsyncContext.CompletionEvent		= Overlapped->hEvent;
	AsyncContext.References			= 1;

	HeapContext = (PKXNETP_GAI_ASYNC_CONTEXT) HeapAlloc(
		GetProcessHeap(), 0, sizeof(*HeapContext));

	if (HeapContext == NULL) {
		SetLastError(WSA_NOT_ENOUGH_MEMORY);
		return WSA_NOT_ENOUGH_MEMORY;
	}

	*HeapContext = AsyncContext;

	if (HeapContext->TimeoutPresent) {
		HeapContext->TimeoutTimer = CreateThreadpoolTimer(
			GetAddrInfoExAsyncTimeoutCallback, HeapContext, NULL);

		if (HeapContext->TimeoutTimer == NULL) {
			GetAddrInfoExDereferenceAsyncContext(HeapContext);
			SetLastError(WSA_NOT_ENOUGH_MEMORY);
			return WSA_NOT_ENOUGH_MEMORY;
		}
	}

	InitializeThreadpoolEnvironment(&CallbackEnvironment);
	SetThreadpoolCallbackRunsLong(&CallbackEnvironment);

	// Keep the setup reference until submission returns, even if the worker
	// finishes immediately. A second reference belongs to the queued worker.
	GetAddrInfoExReferenceAsyncContext(HeapContext);

	// Publish the token and submit while holding the registry lock. A worker
	// can run before submission returns, but completion and cancellation must
	// acquire this lock before touching caller outputs. Thus failed submission
	// cannot race a cancellation callback which frees the handle storage.
	AcquireSRWLockExclusive(&GaiHandleLock);

	if (CancelHandle) {
		if (!GetAddrInfoExRegisterAsyncOperation(HeapContext)) {
			ReleaseSRWLockExclusive(&GaiHandleLock);
			DestroyThreadpoolEnvironment(&CallbackEnvironment);

			if (HeapContext->TimeoutTimer) {
				CloseThreadpoolTimer(HeapContext->TimeoutTimer);
				HeapContext->TimeoutTimer = NULL;
			}

			GetAddrInfoExDereferenceAsyncContext(HeapContext);
			GetAddrInfoExDereferenceAsyncContext(HeapContext);
			SetLastError(WSA_NOT_ENOUGH_MEMORY);
			return WSA_NOT_ENOUGH_MEMORY;
		}

		*CancelHandle = HeapContext->CancelHandle;
	}

	Success = TrySubmitThreadpoolCallback(
		GetAddrInfoExAsyncThreadpoolCallback,
		HeapContext,
		&CallbackEnvironment);

	if (!Success && CancelHandle) {
		// No other registry mutation can have occurred while we hold the lock.
		ASSERT (GaiHandleList == HeapContext);
		GaiHandleList = HeapContext->Next;
		HeapContext->Next = NULL;
		*CancelHandle = NULL;
	}

	ReleaseSRWLockExclusive(&GaiHandleLock);
	DestroyThreadpoolEnvironment(&CallbackEnvironment);

	if (!Success) {
		if (HeapContext->TimeoutTimer) {
			CloseThreadpoolTimer(HeapContext->TimeoutTimer);
			HeapContext->TimeoutTimer = NULL;
		}

		GetAddrInfoExDereferenceAsyncContext(HeapContext); // unused worker reference
		GetAddrInfoExDereferenceAsyncContext(HeapContext); // setup reference
		SetLastError(WSASYSCALLFAILURE);
		return WSASYSCALLFAILURE;
	}

	GetAddrInfoExDereferenceAsyncContext(HeapContext);
	SetLastError(WSA_IO_PENDING);
	return WSA_IO_PENDING;
}

KXNETAPI INT WSAAPI GetAddrInfoExCancel(
	IN	PHANDLE	Handle)
{
	BOOLEAN Success;
	HANDLE CancelHandle;
	PKXNETP_GAI_ASYNC_CONTEXT AsyncContext;

	if (Handle == NULL || *Handle == NULL) {
		return WSA_INVALID_HANDLE;
	}

	CancelHandle = *Handle;
	*Handle = NULL;

	// A handle is an opaque ID. Never cast it to a pointer, even to inspect a
	// magic value: it may be stale, unrelated, or deliberately invalid.
	AsyncContext = GetAddrInfoExLookupAsyncOperation(CancelHandle);

	if (AsyncContext == NULL) {
		return WSA_INVALID_HANDLE;
	}

	Success = GetAddrInfoExCompleteAsyncOperation(AsyncContext, NULL, WSA_E_CANCELLED);
	GetAddrInfoExDereferenceAsyncContext(AsyncContext);

	return Success ? NO_ERROR : WSA_INVALID_HANDLE;
}

KXNETAPI INT WSAAPI GetAddrInfoExOverlappedResult(
	IN	LPOVERLAPPED Overlapped)
{
	if (Overlapped == NULL) {
		return WSAEINVAL;
	}

	ASSERT (Overlapped->Internal <= ULONG_MAX);
	return (INT) Overlapped->Internal;
}