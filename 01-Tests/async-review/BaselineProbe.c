#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shldisp.h>
#include <stdio.h>
#include <stddef.h>

/* Independent C ABI declarations checked against SDK 26100 Boolean
 * specialization and AsyncInfo.h, not implementation-private structures. */
typedef struct OP OP;typedef struct INFO INFO;typedef struct HANDLER HANDLER;
#define INSPECTABLE(T) HRESULT (STDMETHODCALLTYPE *QI)(T*,REFIID,void**);ULONG (STDMETHODCALLTYPE *AddRef)(T*);ULONG (STDMETHODCALLTYPE *Release)(T*);HRESULT (STDMETHODCALLTYPE *GetIids)(T*,ULONG*,IID**);HRESULT (STDMETHODCALLTYPE *Name)(T*,void**);HRESULT (STDMETHODCALLTYPE *Trust)(T*,int*)
typedef struct {INSPECTABLE(OP);HRESULT (STDMETHODCALLTYPE *Put)(OP*,HANDLER*);HRESULT (STDMETHODCALLTYPE *Get)(OP*,HANDLER**);HRESULT (STDMETHODCALLTYPE *Results)(OP*,BYTE*);} OPVT;
typedef struct {INSPECTABLE(INFO);HRESULT (STDMETHODCALLTYPE *Id)(INFO*,ULONG*);HRESULT (STDMETHODCALLTYPE *Status)(INFO*,LONG*);HRESULT (STDMETHODCALLTYPE *Error)(INFO*,HRESULT*);HRESULT (STDMETHODCALLTYPE *Cancel)(INFO*);HRESULT (STDMETHODCALLTYPE *Close)(INFO*);} INFOVT;
typedef struct {HRESULT (STDMETHODCALLTYPE *QI)(HANDLER*,REFIID,void**);ULONG (STDMETHODCALLTYPE *AddRef)(HANDLER*);ULONG (STDMETHODCALLTYPE *Release)(HANDLER*);HRESULT (STDMETHODCALLTYPE *Invoke)(HANDLER*,OP*,LONG);} HANDLERVT;
struct OP {const OPVT *v;};struct INFO {const INFOVT *v;};
struct HANDLER {const HANDLERVT *v;LONG refs;LONG calls;LONG mode;DWORD thread;BOOL value;BOOL agile;IUnknown *ftm;HANDLE ready,proceed;LONG insideGet;LONG insideClose;OP *releaseExternal;};
const IID IID_IInspectable={0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}};
const IID IID_IAgileObject={0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
static const IID AsyncIID={0xcdb5efb3,0x5788,0x509d,{0x9b,0xe1,0x71,0xcc,0xb8,0xa3,0x36,0x2a}};
static const IID HandlerIID={0xc1d3d1a2,0xae17,0x5a5f,{0xb5,0xa2,0xbd,0xcc,0x88,0x44,0x88,0x9a}};
static const IID InfoIID={0x36,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID MarshalIID={3,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID MissingIID={0x51020133,0x3210,0x4567,{8,0,1,2,3,4,5,6}};
#define ILLEGAL ((HRESULT)0x8000000e)
#define DELEGATE ((HRESULT)0x80000018)
HRESULT __stdcall KxCompCreateCompletedBooleanOperation(BOOLEAN,IAsyncOperation**);
static LONG checks,failures,allocations,handlers;
static void check(const char *name,BOOL value){InterlockedIncrement(&checks);if(!value){InterlockedIncrement(&failures);printf("FAIL|%s\n",name);}}
PVOID TestAlloc(SIZE_T bytes){PVOID p=CoTaskMemAlloc(bytes);if(p)InterlockedIncrement(&allocations);return p;}
VOID TestFree(PVOID p){if(p)InterlockedDecrement(&allocations);CoTaskMemFree(p);}
typedef HRESULT (WINAPI *PSTRING)(PCWSTR,UINT,void**);
HRESULT WindowsCreateString(PCWSTR text,UINT count,void **output)
{union{FARPROC raw;PSTRING fn;}native;HMODULE m=GetModuleHandleW(L"combase.dll");native.raw=m?GetProcAddress(m,"WindowsCreateString"):NULL;if(!native.raw)return E_NOTIMPL;return native.fn(text,count,output);}
static ULONG STDMETHODCALLTYPE hAdd(HANDLER *h){return InterlockedIncrement(&h->refs);}
static ULONG STDMETHODCALLTYPE hRelease(HANDLER *h){ULONG n=InterlockedDecrement(&h->refs);if(!n){if(h->ftm)IUnknown_Release(h->ftm);InterlockedDecrement(&handlers);HeapFree(GetProcessHeap(),0,h);}return n;}
static HRESULT STDMETHODCALLTYPE hQI(HANDLER *h,REFIID iid,void **out)
{if(!out)return E_POINTER;*out=NULL;if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&HandlerIID)||(h->agile&&IsEqualIID(iid,&IID_IAgileObject))){*out=h;hAdd(h);return S_OK;}if(h->ftm&&IsEqualIID(iid,&MarshalIID))return IUnknown_QueryInterface(h->ftm,iid,out);return E_NOINTERFACE;}
static HRESULT STDMETHODCALLTYPE hInvoke(HANDLER *h,OP *op,LONG status)
{
 BYTE b[3]={0xa5,0xa5,0xa5};INFO *info=NULL;HANDLER *stored=NULL;IUnknown *identity=NULL,*other=NULL;HRESULT hr;
 InterlockedIncrement(&h->calls);h->thread=GetCurrentThreadId();check("callback status completed",status==1);
 check("callback result byte",op->v->Results(op,b+1)==S_OK&&b[1]==h->value&&b[0]==0xa5&&b[2]==0xa5);
 hr=op->v->Get(op,&stored);h->insideGet=hr;check("reentrant get completed",hr==S_OK&&stored!=NULL);
 if(stored){hQI(h,&IID_IUnknown,(void**)&identity);stored->v->QI(stored,&IID_IUnknown,(void**)&other);check("handler identity",identity==other);IUnknown_Release(identity);IUnknown_Release(other);stored->v->Release(stored);}
 check("reentrant assignment denied",op->v->Put(op,h)==DELEGATE);
 if(h->ready){DWORD index=99;SetEvent(h->ready);hr=CoWaitForMultipleHandles(0,10000,1,&h->proceed,&index);check("callback bounded COM wait",hr==S_OK&&index==0);}
 if(h->mode==1){check("info query in callback",op->v->QI(op,&InfoIID,(void**)&info)==S_OK);if(info){h->insideClose=info->v->Close(info);check("close in callback",h->insideClose==S_OK);info->v->Release(info);}check("closed results rejected",op->v->Results(op,b+1)==ILLEGAL);}
 if(h->releaseExternal){OP *external=h->releaseExternal;h->releaseExternal=NULL;external->v->Release(external);}
 return E_FAIL; /* Notification failure must not change a completed result. */
}
static const HANDLERVT HandlerVtable={hQI,hAdd,hRelease,hInvoke};
static HANDLER *makeHandler(BOOL agile,BOOL value,LONG mode)
{HANDLER *h=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*h));if(!h)return NULL;h->v=&HandlerVtable;h->refs=1;h->agile=agile;h->value=value;h->mode=mode;InterlockedIncrement(&handlers);if(agile&&FAILED(CoCreateFreeThreadedMarshaler((IUnknown*)h,&h->ftm))){hRelease(h);return NULL;}return h;}
static OP *makeOperation(BOOL value)
{OP *op=NULL;HRESULT hr=KxCompCreateCompletedBooleanOperation((BOOLEAN)value,(IAsyncOperation**)&op);check("factory",hr==S_OK&&op!=NULL);return op;}
static void basic(BOOL value)
{
 OP *op=makeOperation(value);INFO *info=NULL;IUnknown *u1=NULL,*u2=NULL;IUnknown *marshal=NULL;HANDLER *h=NULL,*stored=(HANDLER*)(ULONG_PTR)1;IID *iids=NULL;ULONG count=0,id=0;LONG status=0;HRESULT error=E_FAIL;BYTE bytes[3]={0xa5,0xa5,0xa5};void *missing=(void*)(ULONG_PTR)1;int trust=9;
 if(!op)return;
 check("QI info",op->v->QI(op,&InfoIID,(void**)&info)==S_OK&&info!=NULL);
 check("QI unknown",op->v->QI(op,&IID_IUnknown,(void**)&u1)==S_OK);
 check("QI same identity from info",info&&info->v->QI(info,&IID_IUnknown,(void**)&u2)==S_OK&&u1==u2);
 if(u1)IUnknown_Release(u1);if(u2)IUnknown_Release(u2);
 check("QI FTM",op->v->QI(op,&MarshalIID,(void**)&marshal)==S_OK&&marshal!=NULL);if(marshal)IUnknown_Release(marshal);
 check("QI missing clears output",op->v->QI(op,&MissingIID,&missing)==E_NOINTERFACE&&missing==NULL);
 check("IInspectable IID list",op->v->GetIids(op,&count,&iids)==S_OK&&count==2&&IsEqualIID(&iids[0],&AsyncIID)&&IsEqualIID(&iids[1],&InfoIID));if(iids)TestFree(iids);
 check("trust",op->v->Trust(op,&trust)==S_OK&&trust==0);
 check("result scalar boolean",op->v->Results(op,bytes+1)==S_OK&&bytes[1]==value&&bytes[0]==0xa5&&bytes[2]==0xa5);
 check("result E_POINTER",op->v->Results(op,NULL)==E_POINTER);
 check("initial get completed NULL",op->v->Get(op,&stored)==S_OK&&stored==NULL);
 if(info){check("nonzero id",info->v->Id(info,&id)==S_OK&&id!=0);check("completed status",info->v->Status(info,&status)==S_OK&&status==1);check("error success",info->v->Error(info,&error)==S_OK&&error==S_OK);check("cancel completed",info->v->Cancel(info)==S_OK);}
 h=makeHandler(TRUE,value,0);check("handler allocation",h!=NULL);
 if(h){check("put success despite handler HRESULT",op->v->Put(op,h)==S_OK);check("one immediate callback",h->calls==1&&h->thread==GetCurrentThreadId());check("duplicate put rejected",op->v->Put(op,h)==DELEGATE);check("handler released after notification",op->v->Get(op,&stored)==S_OK&&stored==NULL);hRelease(h);}
 if(info){check("close",info->v->Close(info)==S_OK);check("close idempotent",info->v->Close(info)==S_OK);check("get status after close",info->v->Status(info,&status)==ILLEGAL&&status==4);check("get error after close",info->v->Error(info,&error)==ILLEGAL&&error==ILLEGAL);check("cancel closed completed",info->v->Cancel(info)==S_OK);info->v->Release(info);}
 check("results after close",op->v->Results(op,bytes+1)==ILLEGAL);check("get after close",op->v->Get(op,&stored)==ILLEGAL&&stored==NULL);check("put after close",op->v->Put(op,NULL)==ILLEGAL);op->v->Release(op);
}
static void reentrant(void)
{OP *op=makeOperation(TRUE);HANDLER *h;if(!op)return;h=makeHandler(FALSE,TRUE,1);check("STA-compatible plain handler created",h!=NULL);if(h){check("reentrant close callback",op->v->Put(op,h)==S_OK&&h->calls==1);hRelease(h);}op->v->Release(op);
 op=makeOperation(FALSE);if(!op)return;h=makeHandler(TRUE,FALSE,0);if(h){h->releaseExternal=op;check("callback can release external operation",op->v->Put(op,h)==S_OK&&h->calls==1);hRelease(h);}else op->v->Release(op);
 op=makeOperation(FALSE);if(!op)return;check("NULL handler allowed",op->v->Put(op,NULL)==S_OK);h=makeHandler(TRUE,FALSE,0);if(h){check("NULL consumes assignment",op->v->Put(op,h)==DELEGATE&&h->calls==0);hRelease(h);}op->v->Release(op);
}
typedef struct {IStream *stream;HANDLE ready,proceed;BOOL agile;HANDLER *handler;HRESULT unmarshaled,put;DWORD thread;} WORK;
static DWORD WINAPI worker(void *parameter)
{WORK *w=parameter;OP *op=NULL;HANDLER *h;HRESULT init=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);w->thread=GetCurrentThreadId();if(FAILED(init)){w->unmarshaled=init;SetEvent(w->ready);return 1;}w->unmarshaled=CoGetInterfaceAndReleaseStream(w->stream,&AsyncIID,(void**)&op);w->stream=NULL;if(SUCCEEDED(w->unmarshaled)){h=makeHandler(w->agile,TRUE,0);w->handler=h;if(h){h->ready=w->ready;h->proceed=w->proceed;w->put=op->v->Put(op,h);check("worker single callback",h->calls==1&&h->thread==w->thread);hRelease(h);}op->v->Release(op);}SetEvent(w->ready);CoUninitialize();return 0;}
static void apartments(BOOL agile)
{WORK w;OP *op=makeOperation(TRUE);HANDLE thread;HANDLER *stored=NULL;HRESULT hr;DWORD exitCode=99;BYTE b=0;if(!op)return;ZeroMemory(&w,sizeof(w));w.agile=agile;w.ready=CreateEventW(NULL,TRUE,FALSE,NULL);w.proceed=CreateEventW(NULL,TRUE,FALSE,NULL);check("cross-apartment marshal",CoMarshalInterThreadInterfaceInStream(&AsyncIID,(IUnknown*)op,&w.stream)==S_OK);thread=CreateThread(NULL,0,worker,&w,0,NULL);check("worker created",thread!=NULL);if(thread){check("worker callback ready",WaitForSingleObject(w.ready,10000)==WAIT_OBJECT_0);check("real FTM unmarshal",w.unmarshaled==S_OK);hr=op->v->Get(op,&stored);printf("APARTMENT_GET|agile=%u|status=0x%08lx|handler=%u\n",agile,(ULONG)hr,stored!=NULL);if(agile)check("agile delegate cross-apartment get",hr==S_OK&&stored!=NULL);else if(SUCCEEDED(hr)&&stored)check("STA interface returned through proxy",stored!=w.handler);if(stored)stored->v->Release(stored);check("main can read marshaled object",op->v->Results(op,&b)==S_OK&&b==TRUE);SetEvent(w.proceed);check("worker finished",WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);GetExitCodeThread(thread,&exitCode);check("worker result",exitCode==0&&w.put==S_OK);CloseHandle(thread);}CloseHandle(w.ready);CloseHandle(w.proceed);op->v->Release(op);}
int __cdecl main(void)
{HRESULT hr;OP *nil=(OP*)(ULONG_PTR)1;hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(hr))return 2;printf("BEGIN|bits=%u|real_COM_FTM_GIT=1\n",(UINT)(sizeof(void*)*8));check("factory null output",KxCompCreateCompletedBooleanOperation(TRUE,NULL)==E_POINTER);check("fresh counters",allocations==0&&handlers==0);basic(FALSE);basic(TRUE);reentrant();apartments(TRUE);apartments(FALSE);nil=NULL;UNREFERENCED_PARAMETER(nil);check("all own object allocations freed",allocations==0);check("all own handlers freed",handlers==0);printf("SUMMARY|checks=%ld|failures=%ld|live_allocations=%ld|live_handlers=%ld\n",checks,failures,allocations,handlers);CoUninitialize();return failures?1:0;}
