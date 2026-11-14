/* Real owned window/thread/legacy registrations; PnP messages below are
 * explicitly synthetic and sent only to this test's own notification window. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>
#include <dbt.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define KXBASEAPI
#define try __try
#define except __except
/* CM_NOTIFICATION_DECLARATIONS */
CONFIGRET WINAPI CM_Unregister_Notification(HCMNOTIFICATION);
static volatile LONG FailClassRegistration;
static DWORD ClassRegistrationError=ERROR_ACCESS_DENIED;
static DWORD DeviceRegistrationError;
static volatile LONG DeviceRegistrationCalls;
static BOOL FailUser32Initialization,FailWindowCreation;
static volatile LONG DefaultNcDestroy;
static LRESULT WINAPI TrackingDefWindowProcW(HWND Window,UINT Message,WPARAM WParam,LPARAM LParam)
{
    if(Message==WM_NCDESTROY) InterlockedIncrement(&DefaultNcDestroy);
    return DefWindowProcW(Window,Message,WParam,LParam);
}
static ATOM WINAPI FailedRegisterClassW(CONST WNDCLASSW *Class)
{
    UNREFERENCED_PARAMETER(Class);
    SetLastError(ClassRegistrationError);
    return 0;
}
static HDEVNOTIFY WINAPI FailedDeviceRegistration(HANDLE Recipient,LPVOID Filter,DWORD Flags)
{
    UNREFERENCED_PARAMETER(Recipient);UNREFERENCED_PARAMETER(Filter);UNREFERENCED_PARAMETER(Flags);
    InterlockedIncrement(&DeviceRegistrationCalls);
    SetLastError(DeviceRegistrationError);
    return NULL;
}
static HWND WINAPI FailedCreateWindowExW(DWORD ExStyle,LPCWSTR ClassName,LPCWSTR Name,DWORD Style,
    int X,int Y,int Width,int Height,HWND Parent,HMENU Menu,HINSTANCE Instance,LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(ExStyle);UNREFERENCED_PARAMETER(ClassName);UNREFERENCED_PARAMETER(Name);UNREFERENCED_PARAMETER(Style);
    UNREFERENCED_PARAMETER(X);UNREFERENCED_PARAMETER(Y);UNREFERENCED_PARAMETER(Width);UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(Parent);UNREFERENCED_PARAMETER(Menu);UNREFERENCED_PARAMETER(Instance);UNREFERENCED_PARAMETER(Parameter);
    SetLastError(ERROR_SERVICE_SPECIFIC_ERROR);return NULL;
}
static FARPROC WINAPI NotificationGetProcAddress(HMODULE Module,LPCSTR Name)
{
    union { FARPROC Raw; ATOM (WINAPI *RegisterClass)(CONST WNDCLASSW *); LRESULT (WINAPI *DefaultProc)(HWND,UINT,WPARAM,LPARAM); HDEVNOTIFY (WINAPI *RegisterDevice)(HANDLE,LPVOID,DWORD); HWND (WINAPI *CreateWindow)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID); } Function;
    if(!strcmp(Name,"CreateWindowExW")) {
        if(FailUser32Initialization){SetLastError(ERROR_SERVICE_SPECIFIC_ERROR);return NULL;}
        if(FailWindowCreation){Function.CreateWindow=FailedCreateWindowExW;return Function.Raw;}
    }
    if(FailClassRegistration&&!strcmp(Name,"RegisterClassW")) {
        Function.RegisterClass=FailedRegisterClassW;
        return Function.Raw;
    }
    if(!strcmp(Name,"DefWindowProcW")) { Function.DefaultProc=TrackingDefWindowProcW; return Function.Raw; }
    if(DeviceRegistrationError&&!strcmp(Name,"RegisterDeviceNotificationW")) { Function.RegisterDevice=FailedDeviceRegistration;return Function.Raw; }
    return GetProcAddress(Module,Name);
}
#define GetProcAddress NotificationGetProcAddress
/* CM_NOTIFICATION_IMPLEMENTATION */
#undef GetProcAddress

static volatile LONG Assertions, Failures;
static void Check(BOOL Pass,const char *Name)
{
    InterlockedIncrement(&Assertions);
    if (!Pass) { InterlockedIncrement(&Failures); printf("FAIL: %s\n",Name); }
}
typedef struct TEST_CONTEXT {
    HCMNOTIFICATION Expected;
    DWORD CallerThread,CallbackThread,Calls;
    CM_NOTIFY_ACTION Action;
    ULONG Bytes;
    BYTE Data[512];
    BOOL SelfUnregister,Block;
    HANDLE Entered,Release;
} TEST_CONTEXT;
static DWORD CALLBACK Callback(HCMNOTIFICATION Handle,PVOID Parameter,CM_NOTIFY_ACTION Action,PCM_NOTIFY_EVENT_DATA Data,DWORD Bytes)
{
    TEST_CONTEXT *Context=Parameter;
    Check(Context!=NULL,"callback context retained");
    Check(Handle==Context->Expected,"callback registration identity retained");
    Check(GetCurrentThreadId()!=Context->CallerThread,"callback runs on owned worker");
    Check(Data->Reserved==0&&Bytes>=sizeof(*Data),"callback payload reserved/size contract");
    Context->CallbackThread=GetCurrentThreadId(); Context->Action=Action; Context->Bytes=Bytes;
    if(Bytes<=sizeof(Context->Data)) CopyMemory(Context->Data,Data,Bytes);
    ++Context->Calls;
    if(Context->SelfUnregister) Check(CM_Unregister_Notification(Handle)==CR_FAILURE,"self-unregister fails without deadlock");
    if(Context->Block) { SetEvent(Context->Entered); WaitForSingleObject(Context->Release,5000); }
    return ERROR_SUCCESS;
}
typedef struct SEND_CONTEXT { HWND Window; PVOID Payload; HANDLE Returned; } SEND_CONTEXT;
static DWORD WINAPI SendThread(PVOID Parameter)
{
    SEND_CONTEXT *Context=Parameter;
    SendMessageW(Context->Window,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,(LPARAM)Context->Payload);
    SetEvent(Context->Returned); return 0;
}
typedef struct CLOSE_CONTEXT { HCMNOTIFICATION Handle; HANDLE Returned; CONFIGRET Result; } CLOSE_CONTEXT;
static DWORD WINAPI CloseThread(PVOID Parameter)
{
    CLOSE_CONTEXT *Context=Parameter;
    Context->Result=CM_Unregister_Notification(Context->Handle);
    SetEvent(Context->Returned); return 0;
}
static void HandlePayloads(void)
{
    KX_NOTIFICATION Notification;
    BYTE Storage[sizeof(DEV_BROADCAST_HANDLE)+16];
    PDEV_BROADCAST_HANDLE Input=(PDEV_BROADCAST_HANDLE)Storage;
    PCM_NOTIFY_EVENT_DATA Output;
    CM_NOTIFY_ACTION Action;
    DWORD Bytes,Index;
    ULONG Events[]={DBT_DEVICEQUERYREMOVE,DBT_DEVICEQUERYREMOVEFAILED,DBT_DEVICEREMOVEPENDING,DBT_DEVICEREMOVECOMPLETE};
    CM_NOTIFY_ACTION Actions[]={CM_NOTIFY_ACTION_DEVICEQUERYREMOVE,CM_NOTIFY_ACTION_DEVICEQUERYREMOVEFAILED,CM_NOTIFY_ACTION_DEVICEREMOVEPENDING,CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE};
    ZeroMemory(&Notification,sizeof(Notification)); Notification.Filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
    Notification.DeviceNotification=(HDEVNOTIFY)(ULONG_PTR)0x1234;
    ZeroMemory(Storage,sizeof(Storage)); Input->dbch_size=sizeof(Storage); Input->dbch_devicetype=DBT_DEVTYP_HANDLE;
    Input->dbch_hdevnotify=Notification.DeviceNotification;
    for(Index=0;Index<ARRAYSIZE(Events);++Index) {
        Output=KxNotificationCopyEvent(&Notification,Events[Index],(PDEV_BROADCAST_HDR)Input,&Action,&Bytes);
        Check(Output!=NULL&&Action==Actions[Index],"handle action mapping");
        if(Output) { Check(Output->FilterType==CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE&&Output->Reserved==0,"handle payload identity"); HeapFree(GetProcessHeap(),0,Output); }
    }
    Input->dbch_eventguid.Data1=0x12345678; Input->dbch_nameoffset=2;
    Input->dbch_size=FIELD_OFFSET(DEV_BROADCAST_HANDLE,dbch_data)+8;
    for(Index=0;Index<8;++Index) Input->dbch_data[Index]=(BYTE)(Index+1);
    Output=KxNotificationCopyEvent(&Notification,DBT_CUSTOMEVENT,(PDEV_BROADCAST_HDR)Input,&Action,&Bytes);
    Check(Output!=NULL&&Action==CM_NOTIFY_ACTION_DEVICECUSTOMEVENT,"custom action mapping");
    if(Output) {
        Check(Output->DeviceHandle.EventGuid.Data1==0x12345678&&Output->DeviceHandle.NameOffset==2&&Output->DeviceHandle.DataSize==8,"custom metadata preserved");
        Check(!memcmp(Output->DeviceHandle.Data,Input->dbch_data,8),"custom binary data copied");
        HeapFree(GetProcessHeap(),0,Output);
    }
    Input->dbch_hdevnotify=NULL;
    Check(KxNotificationCopyEvent(&Notification,DBT_CUSTOMEVENT,(PDEV_BROADCAST_HDR)Input,&Action,&Bytes)==NULL,"unrelated handle registration rejected");
    Check(KxNotificationCopyEvent(&Notification,DBT_CUSTOMEVENT,(PDEV_BROADCAST_HDR)(ULONG_PTR)1,&Action,&Bytes)==NULL,"inaccessible event pointer caught");
}
static DWORD CALLBACK CancelCallback(HCMNOTIFICATION Handle,PVOID Context,CM_NOTIFY_ACTION Action,PCM_NOTIFY_EVENT_DATA Data,DWORD Bytes)
{
    UNREFERENCED_PARAMETER(Handle); UNREFERENCED_PARAMETER(Context); UNREFERENCED_PARAMETER(Action);
    UNREFERENCED_PARAMETER(Data); UNREFERENCED_PARAMETER(Bytes);
    return ERROR_CANCELLED;
}
static void QueryVeto(void)
{
    KX_NOTIFICATION Notification;
    DEV_BROADCAST_HANDLE Input;
    ZeroMemory(&Notification,sizeof(Notification)); ZeroMemory(&Input,sizeof(Input));
    Notification.Window=(HWND)(ULONG_PTR)0x4321;
    Notification.Filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
    Notification.DeviceNotification=(HDEVNOTIFY)(ULONG_PTR)0x1234; Notification.Callback=CancelCallback;
    Input.dbch_size=sizeof(Input); Input.dbch_devicetype=DBT_DEVTYP_HANDLE; Input.dbch_hdevnotify=Notification.DeviceNotification;
    AcquireSRWLockExclusive(&KxNotificationLock); Notification.Next=KxNotifications; KxNotifications=&Notification; ReleaseSRWLockExclusive(&KxNotificationLock);
    Check(KxNotificationWindowProcedure(Notification.Window,WM_DEVICECHANGE,DBT_DEVICEQUERYREMOVE,(LPARAM)&Input)==BROADCAST_QUERY_DENY,
        "query-remove cancellation is translated into a real veto result");
    Check(KxNotificationWindowProcedure(Notification.Window,WM_DEVICECHANGE,DBT_DEVICEREMOVEPENDING,(LPARAM)&Input)==TRUE,
        "cancellation cannot veto a non-query removal event");
    AcquireSRWLockExclusive(&KxNotificationLock); KxNotifications=Notification.Next; ReleaseSRWLockExclusive(&KxNotificationLock);
}
static void DeviceRegistrationErrors(TEST_CONTEXT *Context)
{
    CM_NOTIFY_FILTER Filter;
    HCMNOTIFICATION Registration;
    HANDLE EventHandle,Nul;
    LONG Before;
    ZeroMemory(&Filter,sizeof(Filter));Filter.cbSize=sizeof(Filter);Filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
    EventHandle=CreateEventW(NULL,FALSE,FALSE,NULL);
    Nul=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    Check(EventHandle!=NULL&&Nul!=INVALID_HANDLE_VALUE,"owned error fixtures opened");
    DeviceRegistrationError=ERROR_SERVICE_SPECIFIC_ERROR;
    Before=DeviceRegistrationCalls;
    SetLastError(0x66554433);
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_INVALID_DATA&&Registration==NULL,
        "NULL device target is exact INVALID_DATA");
    Check(DeviceRegistrationCalls==Before&&GetLastError()==0x66554433,"NULL device target fails before backend and preserves error");
    Filter.DeviceHandle.hTarget=INVALID_HANDLE_VALUE;SetLastError(0x66554433);
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_INVALID_DATA&&Registration==NULL,
        "INVALID_HANDLE_VALUE target is exact INVALID_DATA");
    Check(DeviceRegistrationCalls==Before&&GetLastError()==0x66554433,"invalid device target fails before backend and preserves error");
    Filter.DeviceHandle.hTarget=EventHandle;
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_INVALID_DATA&&Registration==NULL&&GetLastError()==ERROR_SERVICE_SPECIFIC_ERROR,
        "DEVICEHANDLE registration 1066 maps INVALID_DATA and retains native error");
    Filter.DeviceHandle.hTarget=Nul;
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_INVALID_DATA&&Registration==NULL&&GetLastError()==ERROR_SERVICE_SPECIFIC_ERROR,
        "NUL registration 1066 maps INVALID_DATA and retains native error");
    Check(KxNotifications==NULL,"failed device registrations remove list and workers");
    Filter.DeviceHandle.hTarget=EventHandle;DeviceRegistrationError=ERROR_ACCESS_DENIED;
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_ACCESS_DENIED&&Registration==NULL,
        "device registration access denial remains access denial");
    DeviceRegistrationError=ERROR_SERVICE_SPECIFIC_ERROR;Filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    ZeroMemory(&Filter.DeviceInterface,sizeof(Filter.DeviceInterface));
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_FAILURE&&Registration==NULL&&GetLastError()==ERROR_SERVICE_SPECIFIC_ERROR,
        "interface registration 1066 remains generic failure");
    DeviceRegistrationError=0;FailClassRegistration=TRUE;ClassRegistrationError=ERROR_SERVICE_SPECIFIC_ERROR;
    Filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;Filter.DeviceHandle.hTarget=EventHandle;
    Before=DeviceRegistrationCalls;
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_FAILURE&&Registration==NULL&&GetLastError()==ERROR_SERVICE_SPECIFIC_ERROR,
        "DEVICEHANDLE class-setup 1066 remains generic failure");
    Check(DeviceRegistrationCalls==Before,"class-setup failure never invokes registration backend");
    ClassRegistrationError=ERROR_ACCESS_DENIED;FailClassRegistration=FALSE;
    FailWindowCreation=TRUE;
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_FAILURE&&Registration==NULL,
        "DEVICEHANDLE window-setup 1066 remains generic failure");
    Check(DeviceRegistrationCalls==Before,"window-setup failure never invokes registration backend");
    FailWindowCreation=FALSE;FailUser32Initialization=TRUE;
    Check(CM_Register_Notification(&Filter,Context,Callback,&Registration)==CR_FAILURE&&Registration==NULL,
        "DEVICEHANDLE user32-init 1066 remains generic failure");
    Check(DeviceRegistrationCalls==Before,"user32-init failure never invokes registration backend");
    FailUser32Initialization=FALSE;
    Check(KxNotificationError(ERROR_SERVICE_SPECIFIC_ERROR)==CR_FAILURE,"generic 1066 mapper remains unchanged");
    Check(KxNotifications==NULL,"all injected registration errors leave no live registration");
    if(EventHandle)CloseHandle(EventHandle);if(Nul!=INVALID_HANDLE_VALUE)CloseHandle(Nul);
}
int main(void)
{
    CM_NOTIFY_FILTER Filter;
    HCMNOTIFICATION Handle,Stale;
    TEST_CONTEXT Context;
    struct { DEV_BROADCAST_HDR Header; GUID Guid; WCHAR Name[32]; } Event;
    PKX_NOTIFICATION State;
    HWND Window;
    ULONG Index;
    DWORD BeforeHandles,AfterHandles;
    DWORD Deadline;
    BOOL Stopping=FALSE;
    LONG BeforeDefaults;
    HANDLE EventHandle,Sender,Closer;
    SEND_CONTEXT Send;
    CLOSE_CONTEXT Close;
    Check(GetModuleHandleW(L"KexDll.dll")==NULL,"native own-fixture host has no Kex");
    ZeroMemory(&Filter,sizeof(Filter)); Filter.cbSize=sizeof(Filter); Filter.DeviceInterface.ClassGuid.Data1=0x4b455843;
    ZeroMemory(&Context,sizeof(Context)); Context.CallerThread=GetCurrentThreadId();
    Handle=(HCMNOTIFICATION)(ULONG_PTR)1;
    Check(CM_Register_Notification(NULL,&Context,Callback,&Handle)==CR_INVALID_DATA&&Handle==NULL,"NULL filter clears output and fails");
    Check(CM_Register_Notification(&Filter,&Context,NULL,&Handle)==CR_INVALID_DATA,"NULL callback rejected");
    Check(CM_Register_Notification(&Filter,&Context,Callback,NULL)==CR_FAILURE,"NULL output rejected");
    Check(CM_Register_Notification((PCM_NOTIFY_FILTER)(ULONG_PTR)1,&Context,Callback,&Handle)==CR_INVALID_DATA,"inaccessible filter caught");
    Filter.cbSize=sizeof(Filter)-1;
    Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_INVALID_DATA,"size mismatch rejected"); Filter.cbSize=sizeof(Filter);
    Filter.Flags=0x80000000;
    Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_INVALID_DATA,"unknown flags rejected"); Filter.Flags=1;
    Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_INVALID_DATA,"all-interface flag requires zero GUID"); Filter.Flags=0;
    Filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE; Filter.Flags=2;
    ZeroMemory(&Filter.DeviceInstance,sizeof(Filter.DeviceInstance));
    Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_CALL_NOT_IMPLEMENTED&&Handle==NULL,"unsupported instance family never claims registration");
    Filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE; Filter.Flags=0;
    EventHandle=CreateEventW(NULL,FALSE,FALSE,NULL); Filter.DeviceHandle.hTarget=EventHandle;
    Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_INVALID_DATA&&Handle==NULL,"nondevice handle is exact INVALID_DATA with real backend");
    Check(KxNotifications==NULL,"failed registration unwinds list"); CloseHandle(EventHandle);
    DeviceRegistrationErrors(&Context);
    ZeroMemory(&Filter,sizeof(Filter)); Filter.cbSize=sizeof(Filter); Filter.DeviceInterface.ClassGuid.Data1=0x4b455843;
    FailClassRegistration=TRUE;
    Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_ACCESS_DENIED&&Handle==NULL,"worker failure before message queue propagates");
    Check(KxNotifications==NULL,"worker failure before message queue unwinds list");
    FailClassRegistration=FALSE;
    Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_SUCCESS&&Handle!=NULL,"real legacy interface registration succeeds");
    if(!Handle) return 2;
    Context.Expected=Handle; State=(PKX_NOTIFICATION)Handle; Window=State->Window;
    Check(State->DeviceNotification!=NULL&&IsWindow(Window),"real backend has notification and window");
    ZeroMemory(&Event,sizeof(Event)); Event.Header.dbch_size=sizeof(Event); Event.Header.dbch_devicetype=DBT_DEVTYP_DEVICEINTERFACE;
    Event.Guid=Filter.DeviceInterface.ClassGuid; lstrcpyW(Event.Name,L"\\\\?\\VxKexOwnedSyntheticDevice");
    SendMessageW(Window,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,(LPARAM)&Event);
    Check(Context.Calls==1&&Context.Action==CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL,"owned synthetic arrival delivered once");
    Check(!memcmp(&((PCM_NOTIFY_EVENT_DATA)Context.Data)->DeviceInterface.ClassGuid,&Event.Guid,sizeof(GUID))&&
        !lstrcmpW(((PCM_NOTIFY_EVENT_DATA)Context.Data)->DeviceInterface.SymbolicLink,Event.Name),"interface payload copied");
    SendMessageW(Window,WM_DEVICECHANGE,DBT_DEVICEREMOVECOMPLETE,(LPARAM)&Event);
    Check(Context.Calls==2&&Context.Action==CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL,"owned synthetic removal delivered once");
    Event.Guid.Data1++;
    SendMessageW(Window,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,(LPARAM)&Event);
    Check(Context.Calls==2,"mismatched class ignored"); Event.Guid.Data1--;
    Event.Header.dbch_size=sizeof(DEV_BROADCAST_HDR);
    SendMessageW(Window,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,(LPARAM)&Event);
    Check(Context.Calls==2,"short interface payload ignored"); Event.Header.dbch_size=sizeof(Event);
    Context.SelfUnregister=TRUE;
    SendMessageW(Window,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,(LPARAM)&Event);
    Context.SelfUnregister=FALSE;
    Check(Context.Calls==3,"self-unregister attempt leaves registration usable");

    Context.Block=TRUE; Context.Entered=CreateEventW(NULL,TRUE,FALSE,NULL); Context.Release=CreateEventW(NULL,TRUE,FALSE,NULL);
    Send.Window=Window; Send.Payload=&Event; Send.Returned=CreateEventW(NULL,TRUE,FALSE,NULL);
    Close.Handle=Handle; Close.Returned=CreateEventW(NULL,TRUE,FALSE,NULL); Close.Result=CR_FAILURE;
    Sender=CreateThread(NULL,0,SendThread,&Send,0,NULL);
    Check(WaitForSingleObject(Context.Entered,2000)==WAIT_OBJECT_0,"owned callback entered before unregister");
    BeforeDefaults=DefaultNcDestroy;
    Closer=CreateThread(NULL,0,CloseThread,&Close,0,NULL);
    Check(WaitForSingleObject(Close.Returned,80)==WAIT_TIMEOUT,"unregister waits for pending callback");
    Deadline=GetTickCount()+2000;
    do {
        AcquireSRWLockShared(&KxNotificationLock); Stopping=State->Stopping; ReleaseSRWLockShared(&KxNotificationLock);
        if(Stopping) break;
        Sleep(1);
    } while((LONG)(GetTickCount()-Deadline)<0);
    Check(Stopping,"unregister entered stopping state");
    Check(CM_Unregister_Notification(Handle)==CR_INVALID_DATA,"concurrent duplicate unregister rejected");
    SetEvent(Context.Release);
    Check(WaitForSingleObject(Close.Returned,2000)==WAIT_OBJECT_0&&Close.Result==CR_SUCCESS,"unregister completes after callback exits");
    Check(WaitForSingleObject(Sender,2000)==WAIT_OBJECT_0&&WaitForSingleObject(Closer,2000)==WAIT_OBJECT_0,"owned helper threads exit");
    Check(!IsWindow(Window)&&KxNotifications==NULL,"unregister removes owned window and list");
    Check(DefaultNcDestroy>BeforeDefaults,"unregister preserves default WM_NCDESTROY processing");
    Stale=Handle;
    Check(CM_Unregister_Notification(Stale)==CR_INVALID_DATA&&CM_Unregister_Notification(NULL)==CR_INVALID_DATA,"stale/NULL handles safely rejected");
    CloseHandle(Context.Entered); CloseHandle(Context.Release); CloseHandle(Send.Returned); CloseHandle(Close.Returned); CloseHandle(Sender); CloseHandle(Closer);
    Context.Block=FALSE;
    GetProcessHandleCount(GetCurrentProcess(),&BeforeHandles);
    for(Index=0;Index<16;++Index) {
        Check(CM_Register_Notification(&Filter,&Context,Callback,&Handle)==CR_SUCCESS,"repeat registration");
        Check(CM_Unregister_Notification(Handle)==CR_SUCCESS,"repeat unregister");
    }
    GetProcessHandleCount(GetCurrentProcess(),&AfterHandles);
    Check(BeforeHandles==AfterHandles,"repeated registrations leak no kernel handles");
    HandlePayloads();
    QueryVeto();
    printf("cfgmgr notification contract: %ld assertions, %ld failures; synthetic own-window events, no PnP changes\n",Assertions,Failures);
    return Failures?1:0;
}
