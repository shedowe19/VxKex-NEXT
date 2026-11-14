#include "buildcfg.h"
#include "kxbasep.h"
#include <dbt.h>

KXBASEAPI CONFIGRET WINAPI CM_Unregister_Notification(IN HCMNOTIFICATION NotifyContext);

// Do not add a user32 import to KxBase's console and sandbox consumers.
// Resolve the GUI backend only when notification registration is requested.
typedef struct _KX_NOTIFICATION_USER32 {
	ATOM (WINAPI *RegisterClassW)(CONST WNDCLASSW *);
	BOOL (WINAPI *UnregisterClassW)(LPCWSTR, HINSTANCE);
	HWND (WINAPI *CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
	BOOL (WINAPI *DestroyWindow)(HWND);
	LRESULT (WINAPI *DefWindowProcW)(HWND, UINT, WPARAM, LPARAM);
	BOOL (WINAPI *GetMessageW)(LPMSG, HWND, UINT, UINT);
	LRESULT (WINAPI *DispatchMessageW)(CONST MSG *);
	HDEVNOTIFY (WINAPI *RegisterDeviceNotificationW)(HANDLE, LPVOID, DWORD);
	BOOL (WINAPI *UnregisterDeviceNotification)(HDEVNOTIFY);
	BOOL (WINAPI *PostThreadMessageW)(DWORD, UINT, WPARAM, LPARAM);
} KX_NOTIFICATION_USER32;

typedef struct _KX_NOTIFICATION {
	struct _KX_NOTIFICATION *Next;
	LONG References;
	BOOL Stopping;
	HANDLE Thread;
	DWORD ThreadId;
	HANDLE Start, Ready;
	CONFIGRET Result;
	DWORD Error;
	HWND Window;
	HDEVNOTIFY DeviceNotification;
	HMODULE User32Module, CodeModule;
	KX_NOTIFICATION_USER32 User;
	CM_NOTIFY_FILTER Filter;
	PCM_NOTIFY_CALLBACK Callback;
	PVOID Context;
} KX_NOTIFICATION, *PKX_NOTIFICATION;

STATIC SRWLOCK KxNotificationLock = SRWLOCK_INIT;
STATIC PKX_NOTIFICATION KxNotifications;

STATIC CONFIGRET KxNotificationError(IN DWORD Error)
{
	switch (Error) {
	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY: return CR_OUT_OF_MEMORY;
	case ERROR_ACCESS_DENIED: return CR_ACCESS_DENIED;
	case ERROR_INVALID_PARAMETER:
	case ERROR_INVALID_HANDLE: return CR_INVALID_DATA;
	default: return CR_FAILURE;
	}
}

STATIC VOID KxNotificationRelease(IN PKX_NOTIFICATION Notification)
{
	if (InterlockedDecrement(&Notification->References) == 0) {
		if (Notification->Thread) CloseHandle(Notification->Thread);
		else if (Notification->CodeModule) FreeLibrary(Notification->CodeModule);
		if (Notification->Start) CloseHandle(Notification->Start);
		if (Notification->Ready) CloseHandle(Notification->Ready);
		if (Notification->User32Module) FreeLibrary(Notification->User32Module);
		HeapFree(GetProcessHeap(), 0, Notification);
	}
}

STATIC VOID KxNotificationRemove(IN PKX_NOTIFICATION Notification)
{
	PKX_NOTIFICATION *Link;
	BOOL Removed = FALSE;
	AcquireSRWLockExclusive(&KxNotificationLock);
	for (Link = &KxNotifications; *Link; Link = &(*Link)->Next) {
		if (*Link == Notification) {
			*Link = Notification->Next;
			Removed = TRUE;
			break;
		}
	}
	ReleaseSRWLockExclusive(&KxNotificationLock);
	if (Removed) KxNotificationRelease(Notification);
}

// Each cast pairs an export with its exact Windows SDK prototype.
#pragma warning(push)
#pragma warning(disable: 4191)
STATIC BOOL KxNotificationLoadUser32(IN PKX_NOTIFICATION Notification)
{
	HMODULE Module = LoadLibraryW(L"user32.dll");
	Notification->User32Module = Module;
	if (!Module) return FALSE;
#define KX_LOAD_NOTIFICATION_API(Name, Type) \
	Notification->User.Name = (Type) GetProcAddress(Module, #Name); \
	if (!Notification->User.Name) return FALSE
	KX_LOAD_NOTIFICATION_API(RegisterClassW, ATOM (WINAPI *)(CONST WNDCLASSW *));
	KX_LOAD_NOTIFICATION_API(UnregisterClassW, BOOL (WINAPI *)(LPCWSTR, HINSTANCE));
	KX_LOAD_NOTIFICATION_API(CreateWindowExW, HWND (WINAPI *)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID));
	KX_LOAD_NOTIFICATION_API(DestroyWindow, BOOL (WINAPI *)(HWND));
	KX_LOAD_NOTIFICATION_API(DefWindowProcW, LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM));
	KX_LOAD_NOTIFICATION_API(GetMessageW, BOOL (WINAPI *)(LPMSG, HWND, UINT, UINT));
	KX_LOAD_NOTIFICATION_API(DispatchMessageW, LRESULT (WINAPI *)(CONST MSG *));
	KX_LOAD_NOTIFICATION_API(RegisterDeviceNotificationW, HDEVNOTIFY (WINAPI *)(HANDLE, LPVOID, DWORD));
	KX_LOAD_NOTIFICATION_API(UnregisterDeviceNotification, BOOL (WINAPI *)(HDEVNOTIFY));
	KX_LOAD_NOTIFICATION_API(PostThreadMessageW, BOOL (WINAPI *)(DWORD, UINT, WPARAM, LPARAM));
#undef KX_LOAD_NOTIFICATION_API
	return TRUE;
}
#pragma warning(pop)

STATIC PCM_NOTIFY_EVENT_DATA KxNotificationCopyEvent(
	IN PKX_NOTIFICATION Notification, IN WPARAM Event, IN PDEV_BROADCAST_HDR Header,
	OUT CM_NOTIFY_ACTION *Action, OUT DWORD *Bytes)
{
	PCM_NOTIFY_EVENT_DATA Data = NULL;
	ULONG Length, NameBytes, Index;
	PDEV_BROADCAST_DEVICEINTERFACE_W Interface;
	PDEV_BROADCAST_HANDLE Handle;

	// Inspect and copy sender-controlled variable tails under SEH. The caller
	// invokes the application callback outside this exception handler.
	try {
		if (!Header || Header->dbch_size < sizeof(*Header)) return NULL;
		if (Notification->Filter.FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE) {
			if (Header->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE ||
				(Event != DBT_DEVICEARRIVAL && Event != DBT_DEVICEREMOVECOMPLETE)) return NULL;
			if (Header->dbch_size < FIELD_OFFSET(DEV_BROADCAST_DEVICEINTERFACE_W, dbcc_name) + sizeof(WCHAR)) return NULL;
			Interface = (PDEV_BROADCAST_DEVICEINTERFACE_W) Header;
			if (!(Notification->Filter.Flags & 1) &&
				memcmp(&Interface->dbcc_classguid, &Notification->Filter.DeviceInterface.ClassGuid, sizeof(GUID))) return NULL;
			NameBytes = Header->dbch_size - FIELD_OFFSET(DEV_BROADCAST_DEVICEINTERFACE_W, dbcc_name);
			if (NameBytes % sizeof(WCHAR)) return NULL;
			for (Index = 0; Index < NameBytes / sizeof(WCHAR); ++Index) {
				if (!Interface->dbcc_name[Index]) break;
			}
			if (Index == NameBytes / sizeof(WCHAR)) return NULL;
			NameBytes = (Index + 1) * sizeof(WCHAR);
			Length = FIELD_OFFSET(CM_NOTIFY_EVENT_DATA, DeviceInterface.SymbolicLink) + NameBytes;
			if (Length < sizeof(*Data)) Length = sizeof(*Data);
			Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
			if (!Data) return NULL;
			Data->FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
			Data->DeviceInterface.ClassGuid = Interface->dbcc_classguid;
			CopyMemory(Data->DeviceInterface.SymbolicLink, Interface->dbcc_name, NameBytes);
			*Action = Event == DBT_DEVICEARRIVAL ? CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL : CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL;
		} else {
			if (Header->dbch_devicetype != DBT_DEVTYP_HANDLE ||
				Header->dbch_size < FIELD_OFFSET(DEV_BROADCAST_HANDLE, dbch_data)) return NULL;
			Handle = (PDEV_BROADCAST_HANDLE) Header;
			if (Handle->dbch_hdevnotify != Notification->DeviceNotification) return NULL;
			switch (Event) {
			case DBT_DEVICEQUERYREMOVE: *Action = CM_NOTIFY_ACTION_DEVICEQUERYREMOVE; break;
			case DBT_DEVICEQUERYREMOVEFAILED: *Action = CM_NOTIFY_ACTION_DEVICEQUERYREMOVEFAILED; break;
			case DBT_DEVICEREMOVEPENDING: *Action = CM_NOTIFY_ACTION_DEVICEREMOVEPENDING; break;
			case DBT_DEVICEREMOVECOMPLETE: *Action = CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE; break;
			case DBT_CUSTOMEVENT: *Action = CM_NOTIFY_ACTION_DEVICECUSTOMEVENT; break;
			default: return NULL;
			}
			NameBytes = Event == DBT_CUSTOMEVENT ? Header->dbch_size - FIELD_OFFSET(DEV_BROADCAST_HANDLE, dbch_data) : 0;
			if (NameBytes > MAXDWORD - FIELD_OFFSET(CM_NOTIFY_EVENT_DATA, DeviceHandle.Data)) return NULL;
			Length = FIELD_OFFSET(CM_NOTIFY_EVENT_DATA, DeviceHandle.Data) + NameBytes;
			if (Length < sizeof(*Data)) Length = sizeof(*Data);
			Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
			if (!Data) return NULL;
			Data->FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
			if (Event == DBT_CUSTOMEVENT) {
				Data->DeviceHandle.EventGuid = Handle->dbch_eventguid;
				Data->DeviceHandle.NameOffset = Handle->dbch_nameoffset;
				Data->DeviceHandle.DataSize = NameBytes;
				CopyMemory(Data->DeviceHandle.Data, Handle->dbch_data, NameBytes);
			}
		}
		*Bytes = Length;
	} except(EXCEPTION_EXECUTE_HANDLER) {
		if (Data) HeapFree(GetProcessHeap(), 0, Data);
		Data = NULL;
	}
	return Data;
}

STATIC LRESULT CALLBACK KxNotificationWindowProcedure(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	PKX_NOTIFICATION Notification;
	PCM_NOTIFY_EVENT_DATA Data;
	CM_NOTIFY_ACTION Action;
	DWORD Bytes, Result;
	if (Message == WM_NCCREATE) {
		Notification = ((LPCREATESTRUCTW) LParam)->lpCreateParams;
		AcquireSRWLockExclusive(&KxNotificationLock);
		Notification->Window = Window;
		ReleaseSRWLockExclusive(&KxNotificationLock);
		return TRUE;
	}
	AcquireSRWLockShared(&KxNotificationLock);
	for (Notification = KxNotifications; Notification; Notification = Notification->Next) {
		if (Notification->Window == Window) break;
	}
	// Stop callbacks, while retaining default processing for window teardown
	// messages such as WM_NCDESTROY until the worker destroys its window.
	if (!Notification || (Notification->Stopping && Message == WM_DEVICECHANGE)) {
		ReleaseSRWLockShared(&KxNotificationLock);
		return Message == WM_DEVICECHANGE ? TRUE : 0;
	}
	ReleaseSRWLockShared(&KxNotificationLock);
	// Unregistration waits for this worker, including any active callback.
	if (Message == WM_DEVICECHANGE) {
		Data = KxNotificationCopyEvent(Notification, WParam, (PDEV_BROADCAST_HDR) LParam, &Action, &Bytes);
		if (!Data) return TRUE;
		Result = Notification->Callback((HCMNOTIFICATION) Notification, Notification->Context, Action, Data, Bytes);
		HeapFree(GetProcessHeap(), 0, Data);
		if (Action == CM_NOTIFY_ACTION_DEVICEQUERYREMOVE && Result == ERROR_CANCELLED) return BROADCAST_QUERY_DENY;
		return TRUE;
	}
	return Notification->User.DefWindowProcW(Window, Message, WParam, LParam);
}

STATIC DWORD WINAPI KxNotificationThread(IN PVOID Parameter)
{
	PKX_NOTIFICATION Notification = Parameter;
	HMODULE CodeModule = Notification->CodeModule;
	WNDCLASSW Class;
	WCHAR Name[8 + sizeof(ULONG_PTR) * 2 + 1];
	CONST WCHAR Prefix[] = L"VxKexCM_", Hex[] = L"0123456789ABCDEF";
	ULONG Index, Flags = DEVICE_NOTIFY_WINDOW_HANDLE;
	ATOM Atom = 0;
	MSG Message;
	LONG MessageResult;
	DEV_BROADCAST_DEVICEINTERFACE_W Interface;
	DEV_BROADCAST_HANDLE Handle;
	PVOID Filter;
	BOOL RegistrationFailed = FALSE;

	WaitForSingleObject(Notification->Start, INFINITE);
	CopyMemory(Name, Prefix, sizeof(Prefix) - sizeof(WCHAR));
	for (Index = 0; Index < sizeof(ULONG_PTR) * 2; ++Index) {
		Name[8 + sizeof(ULONG_PTR) * 2 - Index - 1] = Hex[((ULONG_PTR) Notification >> (Index * 4)) & 15];
	}
	Name[ARRAYSIZE(Name) - 1] = 0;
	ZeroMemory(&Class, sizeof(Class));
	Class.lpfnWndProc = KxNotificationWindowProcedure;
	Class.hInstance = CodeModule;
	Class.lpszClassName = Name;
	Atom = Notification->User.RegisterClassW(&Class);
	if (!Atom) goto Failed;
	Notification->Window = Notification->User.CreateWindowExW(0, MAKEINTATOM(Atom), L"", 0,
		0, 0, 0, 0, (HWND)(LONG_PTR)-3 /* HWND_MESSAGE */, NULL, CodeModule, Notification);
	if (!Notification->Window) goto Failed;
	if (Notification->Filter.FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE) {
		ZeroMemory(&Interface, sizeof(Interface));
		Interface.dbcc_size = sizeof(Interface);
		Interface.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		Interface.dbcc_classguid = Notification->Filter.DeviceInterface.ClassGuid;
		if (Notification->Filter.Flags & 1) Flags |= DEVICE_NOTIFY_ALL_INTERFACE_CLASSES;
		Filter = &Interface;
	} else {
		ZeroMemory(&Handle, sizeof(Handle));
		Handle.dbch_size = sizeof(Handle);
		Handle.dbch_devicetype = DBT_DEVTYP_HANDLE;
		Handle.dbch_handle = Notification->Filter.DeviceHandle.hTarget;
		Filter = &Handle;
	}
	Notification->DeviceNotification = Notification->User.RegisterDeviceNotificationW(Notification->Window, Filter, Flags);
	if (!Notification->DeviceNotification) {
		RegistrationFailed = TRUE;
		goto Failed;
	}
	Notification->Result = CR_SUCCESS;
	SetEvent(Notification->Ready);
	while ((MessageResult = Notification->User.GetMessageW(&Message, NULL, 0, 0)) > 0) {
		Notification->User.DispatchMessageW(&Message);
	}
	goto Finished;
Failed:
	Notification->Error = GetLastError();
	Notification->Result = KxNotificationError(Notification->Error);
	// Win7's legacy DEVICEHANDLE registration reports invalid targets as 1066,
	// while modern CM reports CR_INVALID_DATA. This empirical mapping applies
	// only to this failed registration, never setup or interface failures.
	// The legacy error cannot distinguish a valid-target service failure.
	if (RegistrationFailed &&
		Notification->Filter.FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE &&
		Notification->Error == ERROR_SERVICE_SPECIFIC_ERROR) {
		Notification->Result = CR_INVALID_DATA;
	}
	SetEvent(Notification->Ready);
Finished:
	if (Notification->DeviceNotification) Notification->User.UnregisterDeviceNotification(Notification->DeviceNotification);
	if (Notification->Window) Notification->User.DestroyWindow(Notification->Window);
	if (Atom) Notification->User.UnregisterClassW(MAKEINTATOM(Atom), CodeModule);
	FreeLibraryAndExitThread(CodeModule, 0);
}

KXBASEAPI CONFIGRET WINAPI CM_Register_Notification(
	IN	PCM_NOTIFY_FILTER	Filter,
	IN	PVOID				Context OPTIONAL,
	IN	PCM_NOTIFY_CALLBACK	Callback,
	OUT	PHCMNOTIFICATION	NotifyContext)
{
	CM_NOTIFY_FILTER LocalFilter;
	PKX_NOTIFICATION Notification;
	CONFIGRET Result;
	DWORD Error;
	GUID ZeroGuid = {0};
	union { LPTHREAD_START_ROUTINE Function; PCWSTR Address; } CodeAddress;
	try {
		if (!NotifyContext) return CR_FAILURE;
		*NotifyContext = NULL;
		if (!Filter || !Callback || Filter->cbSize != sizeof(LocalFilter)) return CR_INVALID_DATA;
		LocalFilter = *Filter;
	} except(EXCEPTION_EXECUTE_HANDLER) {
		return CR_INVALID_DATA;
	}
	if ((ULONG) LocalFilter.FilterType >= CM_NOTIFY_FILTER_TYPE_MAX) return CR_INVALID_DATA;
	if (LocalFilter.FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE) {
		if (LocalFilter.Flags & ~1UL) return CR_INVALID_DATA;
		if ((LocalFilter.Flags & 1) && memcmp(&LocalFilter.DeviceInterface.ClassGuid, &ZeroGuid, sizeof(GUID))) return CR_INVALID_DATA;
	} else if (LocalFilter.FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE) {
		if (LocalFilter.Flags) return CR_INVALID_DATA;
		if (!LocalFilter.DeviceHandle.hTarget || LocalFilter.DeviceHandle.hTarget == INVALID_HANDLE_VALUE) return CR_INVALID_DATA;
	} else {
		if (LocalFilter.Flags & ~2UL) return CR_INVALID_DATA;
		if ((LocalFilter.Flags & 2) && LocalFilter.DeviceInstance.InstanceId[0]) return CR_INVALID_DATA;
		// The legacy API has no equivalent device-instance enumeration/start
		// notification. Do not register an unrelated event and claim success.
		return CR_CALL_NOT_IMPLEMENTED;
	}
	Notification = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Notification));
	if (!Notification) return CR_OUT_OF_MEMORY;
	Notification->References = 2; // registration/list ownership and this caller
	Notification->Filter = LocalFilter;
	Notification->Callback = Callback;
	Notification->Context = Context;
	CodeAddress.Function = KxNotificationThread;
	if (!KxNotificationLoadUser32(Notification) ||
		!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, CodeAddress.Address, &Notification->CodeModule)) goto Failed;
	Notification->Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
	Notification->Start = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!Notification->Ready || !Notification->Start) goto Failed;
	Notification->Thread = CreateThread(NULL, 0, KxNotificationThread, Notification, 0, &Notification->ThreadId);
	if (!Notification->Thread) goto Failed;
	AcquireSRWLockExclusive(&KxNotificationLock);
	Notification->Next = KxNotifications;
	KxNotifications = Notification;
	ReleaseSRWLockExclusive(&KxNotificationLock);
	SetEvent(Notification->Start);
	WaitForSingleObject(Notification->Ready, INFINITE);
	Result = Notification->Result;
	Error = Notification->Error;
	if (Result != CR_SUCCESS) {
		// A failed worker may never have created a message queue. It is already
		// exiting: join it directly rather than posting a quit that could fail.
		WaitForSingleObject(Notification->Thread, INFINITE);
		KxNotificationRemove(Notification);
	} else {
		try { *NotifyContext = (HCMNOTIFICATION) Notification; }
		except(EXCEPTION_EXECUTE_HANDLER) {
			Result = CR_FAILURE;
		}
		if (Result != CR_SUCCESS) CM_Unregister_Notification((HCMNOTIFICATION) Notification);
	}
	KxNotificationRelease(Notification);
	if (Result == CR_SUCCESS) SetLastError(ERROR_SUCCESS);
	else if (Error) SetLastError(Error);
	return Result;
Failed:
	Result = KxNotificationError(GetLastError());
	KxNotificationRelease(Notification);
	KxNotificationRelease(Notification);
	return Result;
}

KXBASEAPI CONFIGRET WINAPI CM_Unregister_Notification(
	IN	HCMNOTIFICATION		NotifyContext)
{
	PKX_NOTIFICATION Notification;
	AcquireSRWLockExclusive(&KxNotificationLock);
	for (Notification = KxNotifications; Notification; Notification = Notification->Next) {
		if ((HCMNOTIFICATION) Notification == NotifyContext) break;
	}
	if (!Notification || Notification->Stopping) {
		ReleaseSRWLockExclusive(&KxNotificationLock);
		return CR_INVALID_DATA;
	}
	// The documented contract forbids synchronous unregister in a callback.
	if (Notification->ThreadId == GetCurrentThreadId()) {
		ReleaseSRWLockExclusive(&KxNotificationLock);
		return CR_FAILURE;
	}
	Notification->Stopping = TRUE;
	ReleaseSRWLockExclusive(&KxNotificationLock);
	if (WaitForSingleObject(Notification->Thread, 0) != WAIT_OBJECT_0 &&
		!Notification->User.PostThreadMessageW(Notification->ThreadId, WM_QUIT, 0, 0) &&
		WaitForSingleObject(Notification->Thread, 0) != WAIT_OBJECT_0) {
		AcquireSRWLockExclusive(&KxNotificationLock);
		Notification->Stopping = FALSE;
		ReleaseSRWLockExclusive(&KxNotificationLock);
		return CR_FAILURE;
	}
	WaitForSingleObject(Notification->Thread, INFINITE);
	KxNotificationRemove(Notification);
	return CR_SUCCESS;
}
