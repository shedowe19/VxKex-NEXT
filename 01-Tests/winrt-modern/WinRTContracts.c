/* Source-inclusion tests. Native COM/URLMON/FTM remain real. Native Windows
 * HSTRING references are kept in a separate API domain below. */
#define KXCOM_WANT_INITGUID
#pragma warning(push,3)
#include "../../01-Extended DLLs/KxCom/buildcfg.h"
#include "../../01-Extended DLLs/KxCom/kxcomp.h"
#include <stdio.h>
#include <wchar.h>
#include <ObjIdl.h>
#pragma warning(pop)

typedef struct MEMORY_RECORD {struct MEMORY_RECORD *Next;PVOID Pointer;} MEMORY_RECORD;
static CRITICAL_SECTION MemoryLock;
static MEMORY_RECORD *MemoryRecords;
static volatile LONG Checks,Failures,OwnedMemory,LiveInstanceMarshalers,LiveCachedMarshalers;
static volatile LONG FailFtm,FtmCreated,FtmFreed,BarrierCount;
static HANDLE FtmBarrier;
static LONG FtmBarrierTarget;
static void Check(BOOL Pass,PCSTR Name)
{InterlockedIncrement(&Checks);if(!Pass){InterlockedIncrement(&Failures);printf("FAIL|%s\n",Name);}}
static void Record(PVOID Pointer)
{
    MEMORY_RECORD *Entry;
    if(!Pointer)return;
    Entry=HeapAlloc(GetProcessHeap(),0,sizeof(*Entry));if(!Entry)ExitProcess(90);
    Entry->Pointer=Pointer;EnterCriticalSection(&MemoryLock);Entry->Next=MemoryRecords;MemoryRecords=Entry;LeaveCriticalSection(&MemoryLock);
    InterlockedIncrement(&OwnedMemory);
}
static void Forget(PVOID Pointer)
{
    MEMORY_RECORD **Link,*Entry=NULL;
    if(!Pointer)return;
    EnterCriticalSection(&MemoryLock);
    for(Link=&MemoryRecords;*Link;Link=&(*Link)->Next)if((*Link)->Pointer==Pointer){Entry=*Link;*Link=Entry->Next;break;}
    LeaveCriticalSection(&MemoryLock);
    if(Entry){InterlockedDecrement(&OwnedMemory);HeapFree(GetProcessHeap(),0,Entry);}
}
static PVOID TrackedHeapAlloc(HANDLE Heap,ULONG Flags,SIZE_T Bytes)
{PVOID Memory=RtlAllocateHeap(Heap,Flags,Bytes);Record(Memory);return Memory;}
static VOID TrackedHeapFree(HANDLE Heap,ULONG Flags,PVOID Memory)
{Forget(Memory);RtlFreeHeap(Heap,Flags,Memory);}
static PVOID TrackedCoAlloc(SIZE_T Bytes)
{PVOID Memory=CoTaskMemAlloc(Bytes);Record(Memory);return Memory;}
static VOID TrackedCoFree(PVOID Memory)
{Forget(Memory);CoTaskMemFree(Memory);}
static HRESULT WINAPI TrackedCreateFtm(IUnknown*,IUnknown**);

#undef KexLogEvent
#define KexLogEvent(...) ((void)0)
#undef KexDebugCheckpoint
#define KexDebugCheckpoint() ((void)0)
#undef SafeAllocEx
#define SafeAllocEx(Heap,Flags,Type,Count) ((Type*)TrackedHeapAlloc(Heap,Flags,sizeof(Type)*(Count)))
#undef SafeFreeEx
#define SafeFreeEx(Heap,Flags,Pointer) do{TrackedHeapFree(Heap,Flags,Pointer);(Pointer)=NULL;}while(0)
#define CoTaskMemAlloc TrackedCoAlloc
#define CoTaskMemFree TrackedCoFree
#define CoCreateFreeThreadedMarshaler TrackedCreateFtm
#pragma warning(push,3)
#include "../../01-Extended DLLs/KxCom/winrt.c"
#include "../../01-Extended DLLs/KxCom/actfact.c"
#include "../../01-Extended DLLs/KxCom/rofactry.c"
#include "../../01-Extended DLLs/KxCom/roinit.c"
#include "../../01-Extended DLLs/KxCom/uri.c"
#include "../../01-Extended DLLs/KxCom/glprstat.c"
#include "../../01-Extended DLLs/KxCom/uisettings.c"
#include "../../01-Extended DLLs/KxCom/uiview.c"
#include "../../01-Extended DLLs/KxCom/asyncbool.c"
#include "../../01-Extended DLLs/KxCom/launcher.c"
#pragma warning(pop)
#undef CoTaskMemAlloc
#undef CoTaskMemFree
#undef CoCreateFreeThreadedMarshaler

typedef struct MARSHAL_TRACKER {const IUnknownVtbl *lpVtbl;LONG References;IUnknown *Real;BOOL Cached;} MARSHAL_TRACKER;
static HRESULT STDMETHODCALLTYPE MarshalQI(IUnknown *This,REFIID Iid,void **Out)
{MARSHAL_TRACKER *Tracker=(MARSHAL_TRACKER*)This;return Tracker->Real->lpVtbl->QueryInterface(Tracker->Real,Iid,Out);}
static ULONG STDMETHODCALLTYPE MarshalAdd(IUnknown *This)
{MARSHAL_TRACKER *Tracker=(MARSHAL_TRACKER*)This;Tracker->Real->lpVtbl->AddRef(Tracker->Real);return InterlockedIncrement(&Tracker->References);}
static ULONG STDMETHODCALLTYPE MarshalRelease(IUnknown *This)
{
    MARSHAL_TRACKER *Tracker=(MARSHAL_TRACKER*)This;
    ULONG Remaining=InterlockedDecrement(&Tracker->References);
    Tracker->Real->lpVtbl->Release(Tracker->Real);
    if(!Remaining){InterlockedIncrement(&FtmFreed);InterlockedDecrement(Tracker->Cached?&LiveCachedMarshalers:&LiveInstanceMarshalers);HeapFree(GetProcessHeap(),0,Tracker);}
    return Remaining;
}
static const IUnknownVtbl MarshalVtbl={MarshalQI,MarshalAdd,MarshalRelease};
static HRESULT WINAPI TrackedCreateFtm(IUnknown *Outer,IUnknown **Out)
{
    HRESULT Result;
    IUnknown *Real=NULL;
    MARSHAL_TRACKER *Tracker;
    ULONG Index;
    BOOL Cached=Outer==(IUnknown*)&CUISettings||Outer==(IUnknown*)&CUIViewSettings;
    if(!Out)return E_POINTER;*Out=NULL;
    if(FailFtm)return E_OUTOFMEMORY;
    Result=CoCreateFreeThreadedMarshaler(Outer,&Real);if(FAILED(Result))return Result;
    for(Index=0;Index<KxRuntimeClassCount;++Index)if(Outer==(IUnknown*)&ClassFactories[Index])Cached=TRUE;
    Tracker=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*Tracker));if(!Tracker){Real->lpVtbl->Release(Real);return E_OUTOFMEMORY;}
    Tracker->lpVtbl=&MarshalVtbl;Tracker->References=1;Tracker->Real=Real;Tracker->Cached=Cached;
    InterlockedIncrement(&FtmCreated);InterlockedIncrement(Cached?&LiveCachedMarshalers:&LiveInstanceMarshalers);
    if(FtmBarrier){if(InterlockedIncrement(&BarrierCount)==FtmBarrierTarget)SetEvent(FtmBarrier);Check(WaitForSingleObject(FtmBarrier,5000)==WAIT_OBJECT_0,"bounded concurrent FTM constructors");}
    *Out=(IUnknown*)Tracker;return S_OK;
}

static const IID TypedVector={0x2f13c006,0xa03a,0x5f69,{0xb0,0x90,0x75,0xa4,0x3e,0x33,0x42,0x3e}};
static PCWSTR Names[]={L"Windows.Foundation.Uri",L"Windows.UI.ViewManagement.UISettings",L"Windows.System.UserProfile.GlobalizationPreferences",L"Windows.UI.ViewManagement.UIViewSettings",L"Windows.System.Launcher"};
static const IID *Specific[]={&IID_IUriRuntimeClassFactory,&IID_IUISettings3,&IID_IGlobalizationPreferencesStatics,&IID_IUIViewSettingsInterop,&IID_ILauncherStatics};
static HSTRING Make(PCWSTR Text)
{HSTRING String=NULL;Check(WindowsCreateString(Text,(ULONG)wcslen(Text),&String)==S_OK,"candidate HSTRING create");return String;}
static IActivationFactory *Factory(ULONG Index)
{
    HSTRING Name=Make(Names[Index]);IActivationFactory *Object=NULL;
    Check(RoGetActivationFactory(Name,&IID_IActivationFactory,(void**)&Object)==S_OK&&Object!=NULL,"class-specific factory");WindowsDeleteString(Name);return Object;
}
static BOOL TextEquals(HSTRING String,PCWSTR Text)
{return WindowsGetStringLen(String)==wcslen(Text)&&CompareStringOrdinal(WindowsGetStringRawBuffer(String,NULL),-1,Text,-1,FALSE)==CSTR_EQUAL;}

/* Native reference inputs are opaque PVOID handles and use only these
 * explicitly resolved native functions. They never enter production winrt.c. */
typedef union NATIVE_FUNCTION {
    FARPROC Raw;
    HRESULT (WINAPI *Make)(PCWSTR,UINT,PVOID*);
    HRESULT (WINAPI *Delete)(PVOID);
    PCWSTR (WINAPI *Text)(PVOID,UINT*);
    HRESULT (WINAPI *Compare)(PVOID,PVOID,INT*);
    HRESULT (WINAPI *Initialize)(INT);
    VOID (WINAPI *Uninitialize)(VOID);
    HRESULT (WINAPI *Factory)(PVOID,REFIID,PVOID*);
} NATIVE_FUNCTION;
static struct {HMODULE Module;NATIVE_FUNCTION Make,Delete,Text,Compare,Initialize,Uninitialize,Factory;BOOL Initialized;} Native;
static void NativeOpen(void)
{
    WCHAR Path[MAX_PATH];HRESULT Result;
    Check(GetSystemDirectoryW(Path,ARRAYSIZE(Path))!=0&&wcscat_s(Path,ARRAYSIZE(Path),L"\\combase.dll")==0,"native COMBASE path");
    Native.Module=LoadLibraryW(Path);Check(Native.Module!=NULL,"native Windows HSTRING reference available");
    if(!Native.Module)ExitProcess(3);
#define LOAD_NATIVE(Member,Name) Native.Member.Raw=GetProcAddress(Native.Module,Name);Check(Native.Member.Raw!=NULL,"native reference export " Name);if(!Native.Member.Raw)ExitProcess(3)
    LOAD_NATIVE(Make,"WindowsCreateString");LOAD_NATIVE(Delete,"WindowsDeleteString");LOAD_NATIVE(Text,"WindowsGetStringRawBuffer");LOAD_NATIVE(Compare,"WindowsCompareStringOrdinal");LOAD_NATIVE(Initialize,"RoInitialize");LOAD_NATIVE(Uninitialize,"RoUninitialize");LOAD_NATIVE(Factory,"RoGetActivationFactory");
#undef LOAD_NATIVE
    Result=Native.Initialize.Initialize(0);Check(SUCCEEDED(Result),"native reference initializes same STA");Native.Initialized=SUCCEEDED(Result);
}
static void NativeClose(void)
{if(Native.Initialized)Native.Uninitialize.Uninitialize();if(Native.Module)FreeLibrary(Native.Module);ZeroMemory(&Native,sizeof(Native));}
static void StringComparisonContracts(void)
{
    static PCWSTR Values[]={NULL,L"",L"alpha",L"beta",L"ALPHA"};
    ULONG First,Second,Compared=0;HSTRING Candidate1,Candidate2;PVOID Native1,Native2;INT Expected,Actual;HRESULT ExpectedStatus,ActualStatus;
    for(First=0;First<ARRAYSIZE(Values);++First)for(Second=0;Second<ARRAYSIZE(Values);++Second){
        Candidate1=Candidate2=NULL;Native1=Native2=NULL;
        if(Values[First]){WindowsCreateString(Values[First],(ULONG)wcslen(Values[First]),&Candidate1);Native.Make.Make(Values[First],(UINT)wcslen(Values[First]),&Native1);}
        if(Values[Second]){WindowsCreateString(Values[Second],(ULONG)wcslen(Values[Second]),&Candidate2);Native.Make.Make(Values[Second],(UINT)wcslen(Values[Second]),&Native2);}
        Expected=Actual=99;ExpectedStatus=Native.Compare.Compare(Native1,Native2,&Expected);ActualStatus=WindowsCompareStringOrdinal(Candidate1,Candidate2,&Actual);
        Check(ExpectedStatus==S_OK&&ActualStatus==ExpectedStatus&&Actual==Expected,"null/empty/nonempty ordinal comparison native parity");++Compared;
        WindowsDeleteString(Candidate1);WindowsDeleteString(Candidate2);Native.Delete.Delete(Native1);Native.Delete.Delete(Native2);
    }
    printf("NATIVE_STRING_COMPARE|pairs=%lu|inputs_separate=1\n",Compared);
}
static void NativeUriReference(void)
{
    static const WCHAR Input[]=L"https://example.invalid/a/b\0hidden";
    PVOID Class=NULL,Text=NULL,FactoryObject=NULL,Uri=NULL,Output=NULL,Relative=NULL;
    UINT Length=0;HRESULT Result;
    union {PVOID Address;HRESULT (STDMETHODCALLTYPE *Create)(PVOID,PVOID,PVOID*);HRESULT (STDMETHODCALLTYPE *Relative)(PVOID,PVOID,PVOID,PVOID*);HRESULT (STDMETHODCALLTYPE *Absolute)(PVOID,PVOID*);} Method;
    Native.Make.Make(Names[0],(UINT)wcslen(Names[0]),&Class);
    Result=Native.Factory.Factory(Class,&IID_IUriRuntimeClassFactory,&FactoryObject);
    Check(Result==S_OK&&FactoryObject!=NULL,"native URI shape oracle");
    Native.Delete.Delete(Class);
    if(!FactoryObject)return;
    Native.Make.Make(Input,ARRAYSIZE(Input)-1,&Text);
    Check(Native.Text.Text(Text,&Length)!=NULL&&Length==ARRAYSIZE(Input)-1,"native HSTRING retains full embedded-NUL length");
    Method.Address=(*(PVOID**)FactoryObject)[6];Result=Method.Create(FactoryObject,Text,&Uri);
    Check(Result==S_OK&&Uri!=NULL,"native URI accepts embedded-NUL input");
    if(Uri){Method.Address=(*(PVOID**)Uri)[6];Result=Method.Absolute(Uri,&Output);Check(Result==S_OK&&CompareStringOrdinal(Native.Text.Text(Output,NULL),-1,L"https://example.invalid/a/b",-1,FALSE)==CSTR_EQUAL,"native URI truncates URL at NUL");Native.Delete.Delete(Output);((IUnknown*)Uri)->lpVtbl->Release((IUnknown*)Uri);Uri=NULL;}
    Native.Make.Make(L"c?x=1",5,&Relative);Method.Address=(*(PVOID**)FactoryObject)[7];Result=Method.Relative(FactoryObject,Text,Relative,&Uri);
    Check(Result==S_OK&&Uri!=NULL,"native relative URI accepts embedded-NUL base");
    if(Uri){Output=NULL;Method.Address=(*(PVOID**)Uri)[6];Result=Method.Absolute(Uri,&Output);Check(Result==S_OK&&CompareStringOrdinal(Native.Text.Text(Output,NULL),-1,L"https://example.invalid/a/c?x=1",-1,FALSE)==CSTR_EQUAL,"native relative URI parsed prefix value");Native.Delete.Delete(Output);((IUnknown*)Uri)->lpVtbl->Release((IUnknown*)Uri);}
    Native.Delete.Delete(Text);Native.Delete.Delete(Relative);((IUnknown*)FactoryObject)->lpVtbl->Release((IUnknown*)FactoryObject);
    printf("NATIVE_URI_REFERENCE|opaque_native_inputs=1|nul_truncation=1\n");
}
static void IdList(IInspectable *Object,ULONG Expected)
{
    IID *Ids=NULL;ULONG Count=0,Index;void *Interface=NULL;
    Check(Object->lpVtbl->GetIids(Object,&Count,&Ids)==S_OK&&Count==Expected&&Ids!=NULL,"IID array count/allocation");
    if(Ids){for(Index=0;Index<Count;++Index){Interface=NULL;Check(Object->lpVtbl->QueryInterface(Object,&Ids[Index],&Interface)==S_OK&&Interface!=NULL,"every advertised IID queries");if(Interface)((IUnknown*)Interface)->lpVtbl->Release((IUnknown*)Interface);}TrackedCoFree(Ids);}
}
static void ClassContracts(void)
{
    ULONG Class,Type;IActivationFactory *Object;IInspectable *Instance,*Inspect;
    HSTRING Name,RuntimeName;HRESULT Result;IUnknown *Identity,*Again;void *Interface;
    for(Class=0;Class<ARRAYSIZE(Names);++Class){
        Object=Factory(Class);if(!Object)continue;
        IdList((IInspectable*)Object,Class==1?1:2);
        RuntimeName=(HSTRING)(ULONG_PTR)1;Check(Object->lpVtbl->GetRuntimeClassName(Object,&RuntimeName)==(HRESULT)0x8000000eL&&RuntimeName==NULL,"factory runtime name illegal-call contract");
        for(Type=0;Type<ARRAYSIZE(Specific);++Type){
            Interface=(void*)(ULONG_PTR)1;Result=Object->lpVtbl->QueryInterface(Object,Specific[Type],&Interface);
            Check(Type==Class&&Class!=1?Result==S_OK&&Interface!=NULL:Result==E_NOINTERFACE&&Interface==NULL,"factory rejects unrelated class IIDs");
            if(SUCCEEDED(Result)&&Interface){Identity=NULL;Again=NULL;Object->lpVtbl->QueryInterface(Object,&IID_IUnknown,(void**)&Identity);((IUnknown*)Interface)->lpVtbl->QueryInterface((IUnknown*)Interface,&IID_IUnknown,(void**)&Again);Check(Identity==Again,"factory/statics COM identity");if(Identity)Identity->lpVtbl->Release(Identity);if(Again)Again->lpVtbl->Release(Again);((IUnknown*)Interface)->lpVtbl->Release((IUnknown*)Interface);}
        }
        Instance=(IInspectable*)(ULONG_PTR)1;Result=Object->lpVtbl->ActivateInstance(Object,&Instance);
        Check(Class==1?Result==S_OK&&Instance!=NULL:Result==E_NOTIMPL&&Instance==NULL,"class-specific activation");
        if(SUCCEEDED(Result)&&Instance){IdList(Instance,6);Inspect=NULL;Check(Instance->lpVtbl->QueryInterface(Instance,&IID_IUISettings3,(void**)&Inspect)==S_OK,"UISettings activation interface");if(Inspect)Inspect->lpVtbl->Release(Inspect);Instance->lpVtbl->Release(Instance);}
        Object->lpVtbl->Release(Object);
    }
    Name=Make(L"Codex.WinRT.Missing");Interface=(void*)(ULONG_PTR)1;Check(RoGetActivationFactory(Name,&IID_IActivationFactory,&Interface)==REGDB_E_CLASSNOTREG&&Interface==NULL,"unknown class error/cleared output");WindowsDeleteString(Name);
    Name=Make(L"windows.foundation.uri");Interface=(void*)(ULONG_PTR)1;Check(RoGetActivationFactory(Name,&IID_IActivationFactory,&Interface)==REGDB_E_CLASSNOTREG&&Interface==NULL,"class identifiers case sensitive");WindowsDeleteString(Name);
    {static const WCHAR Text[]=L"Windows.Foundation.Uri\0Suffix";Name=NULL;WindowsCreateString(Text,ARRAYSIZE(Text)-1,&Name);Interface=(void*)(ULONG_PTR)1;Check(RoGetActivationFactory(Name,&IID_IActivationFactory,&Interface)==E_INVALIDARG&&Interface==NULL,"class identifier rejects embedded NUL");WindowsDeleteString(Name);}
    Check(RoGetActivationFactory(NULL,&IID_IActivationFactory,&Interface)==E_INVALIDARG&&Interface==NULL,"empty class identifier");
    Check(RoGetActivationFactory(NULL,&IID_IActivationFactory,NULL)==E_POINTER,"factory output pointer required");
}

typedef struct FOREIGN_URI {IUriRuntimeClassVtbl *lpVtbl;ULONG_PTR Poison;PCWSTR Text;ULONG Length,Calls;LONG References;} FOREIGN_URI;
static ULONG STDMETHODCALLTYPE ForeignAdd(IUriRuntimeClass *Object)
{return InterlockedIncrement(&((FOREIGN_URI*)Object)->References);}
static ULONG STDMETHODCALLTYPE ForeignRelease(IUriRuntimeClass *Object)
{return InterlockedDecrement(&((FOREIGN_URI*)Object)->References);}
static HRESULT STDMETHODCALLTYPE ForeignQuery(IUriRuntimeClass *Object,REFIID Iid,void **Out)
{if(!Out)return E_POINTER;*Out=NULL;if(IsEqualIID(Iid,&IID_IUnknown)||IsEqualIID(Iid,&IID_IInspectable)||IsEqualIID(Iid,&IID_IUriRuntimeClass)){*Out=Object;ForeignAdd(Object);return S_OK;}return E_NOINTERFACE;}
static HRESULT STDMETHODCALLTYPE ForeignIids(IUriRuntimeClass *Object,PULONG Count,IID **Ids)
{UNREFERENCED_PARAMETER(Object);if(!Count||!Ids)return E_POINTER;*Count=0;*Ids=NULL;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE ForeignString(IUriRuntimeClass *Object,HSTRING *Out)
{UNREFERENCED_PARAMETER(Object);if(!Out)return E_POINTER;*Out=NULL;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE ForeignTrust(IUriRuntimeClass *Object,TrustLevel *Out)
{UNREFERENCED_PARAMETER(Object);if(!Out)return E_POINTER;*Out=BaseTrust;return S_OK;}
static HRESULT STDMETHODCALLTYPE ForeignUnknown(IUriRuntimeClass *Object,IUnknown **Out)
{UNREFERENCED_PARAMETER(Object);if(!Out)return E_POINTER;*Out=NULL;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE ForeignPort(IUriRuntimeClass *Object,PULONG Out)
{UNREFERENCED_PARAMETER(Object);if(!Out)return E_POINTER;*Out=0;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE ForeignBool(IUriRuntimeClass *Object,PBOOLEAN Out)
{UNREFERENCED_PARAMETER(Object);if(!Out)return E_POINTER;*Out=FALSE;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE ForeignEquals(IUriRuntimeClass *Object,IUriRuntimeClass *Other,PBOOLEAN Out)
{UNREFERENCED_PARAMETER(Other);return ForeignBool(Object,Out);}
static HRESULT STDMETHODCALLTYPE ForeignCombine(IUriRuntimeClass *Object,HSTRING Extra,IUriRuntimeClass **Out)
{UNREFERENCED_PARAMETER(Object);UNREFERENCED_PARAMETER(Extra);if(!Out)return E_POINTER;*Out=NULL;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE ForeignAbsolute(IUriRuntimeClass *Object,HSTRING *Output)
{FOREIGN_URI *Foreign=(FOREIGN_URI*)Object;++Foreign->Calls;return WindowsCreateString(Foreign->Text,Foreign->Length,Output);}
static IUriRuntimeClassVtbl ForeignVtbl={ForeignQuery,ForeignAdd,ForeignRelease,ForeignIids,ForeignString,ForeignTrust,
    ForeignAbsolute,ForeignString,ForeignString,ForeignString,ForeignString,ForeignString,ForeignString,ForeignString,
    ForeignString,ForeignUnknown,ForeignString,ForeignString,ForeignString,ForeignPort,ForeignBool,ForeignEquals,ForeignCombine};
static void UriContracts(void)
{
    IUriRuntimeClassFactory *Create=NULL;IUriRuntimeClass *Uri=NULL,*Combined=NULL;HSTRING Base,Extra,Text,Embedded;BOOLEAN Equal=FALSE;FOREIGN_URI Foreign;LONG Before;
    Base=Make(L"https://example.invalid/a/b");Extra=Make(L"c?x=1");
    Check(KxCompQueryBuiltinFactory(KxRuntimeUri,&IID_IUriRuntimeClassFactory,(void**)&Create)==S_OK,"URI factory interface");
    Check(Create->lpVtbl->CreateUri(Create,Base,&Uri)==S_OK&&Uri!=NULL,"real URLMON URI creation");
    if(Uri){
        Text=NULL;Check(Uri->lpVtbl->get_AbsoluteUri(Uri,&Text)==S_OK&&TextEquals(Text,L"https://example.invalid/a/b"),"URI absolute value");WindowsDeleteString(Text);
        Check(Uri->lpVtbl->CombineUri(Uri,Extra,&Combined)==S_OK&&Combined!=NULL,"URI combine");
        if(Combined){Text=NULL;Check(Combined->lpVtbl->get_AbsoluteUri(Combined,&Text)==S_OK&&TextEquals(Text,L"https://example.invalid/a/c?x=1"),"URI combined value");WindowsDeleteString(Text);Combined->lpVtbl->Release(Combined);Combined=NULL;}
        ZeroMemory(&Foreign,sizeof(Foreign));Foreign.lpVtbl=&ForeignVtbl;Foreign.Poison=1;Foreign.Text=L"https://example.invalid/a/b";Foreign.Length=(ULONG)wcslen(Foreign.Text);Foreign.References=1;
        Check(Uri->lpVtbl->Equals(Uri,(IUriRuntimeClass*)&Foreign,&Equal)==S_OK&&Equal&&Foreign.Calls==1&&Foreign.Poison==1,"foreign URI uses public property, not private payload");
        {static const WCHAR Nul[]=L"https://example.invalid/a/b\0hidden";Foreign.Text=Nul;Foreign.Length=ARRAYSIZE(Nul)-1;Equal=FALSE;Check(Uri->lpVtbl->Equals(Uri,(IUriRuntimeClass*)&Foreign,&Equal)==S_OK&&Equal,"foreign embedded NUL follows URL parser truncation");}
        Check(Foreign.References==1,"foreign public object lifetime retained correctly");
        Uri->lpVtbl->Release(Uri);Uri=NULL;
    }
    Check(Create->lpVtbl->CreateWithRelativeUri(Create,Base,Extra,&Uri)==S_OK&&Uri!=NULL,"URI relative constructor");if(Uri){Text=NULL;Check(Uri->lpVtbl->get_AbsoluteUri(Uri,&Text)==S_OK&&TextEquals(Text,L"https://example.invalid/a/c?x=1"),"relative constructor value");WindowsDeleteString(Text);Uri->lpVtbl->Release(Uri);Uri=NULL;}
    {static const WCHAR Nul[]=L"https://example.invalid/a\0hidden";Embedded=NULL;WindowsCreateString(Nul,ARRAYSIZE(Nul)-1,&Embedded);Check(Create->lpVtbl->CreateUri(Create,Embedded,&Uri)==S_OK&&Uri!=NULL,"candidate embedded NUL constructor accepts prefix");if(Uri){Text=NULL;Check(Uri->lpVtbl->get_AbsoluteUri(Uri,&Text)==S_OK&&TextEquals(Text,L"https://example.invalid/a"),"candidate embedded NUL truncated value");WindowsDeleteString(Text);Uri->lpVtbl->Release(Uri);Uri=NULL;}WindowsDeleteString(Embedded);}
    Before=OwnedMemory;FailFtm=TRUE;Check(Create->lpVtbl->CreateUri(Create,Base,&Uri)==E_OUTOFMEMORY&&Uri==NULL,"URI FTM init failure not published");FailFtm=FALSE;Check(OwnedMemory==Before&&LiveInstanceMarshalers==0,"URI init failure cleans native URI/payload");
    Create->lpVtbl->Release(Create);WindowsDeleteString(Base);WindowsDeleteString(Extra);
}

static void VectorContracts(void)
{
    IGlobalizationPreferencesStatics *Global=NULL;IVectorView_HSTRING *Vector=NULL;HSTRING Item=(HSTRING)(ULONG_PTR)1,Items[3]={NULL,NULL,NULL};ULONG Count,Size=0,Index;LONG Before;
    Check(IsEqualIID(&TypedVector,&IID_IVectorView),"closed HSTRING vector IID");
    Check(KxCompQueryBuiltinFactory(KxRuntimeGlobalizationPreferences,&IID_IGlobalizationPreferencesStatics,(void**)&Global)==S_OK,"globalization factory interface");
    Before=OwnedMemory;FailFtm=TRUE;Check(Global->lpVtbl->get_Languages(Global,&Vector)==E_OUTOFMEMORY&&Vector==NULL,"vector FTM init failure not published");FailFtm=FALSE;Check(OwnedMemory==Before,"vector init failure frees HSTRING/array/payload");
    Check(Global->lpVtbl->get_Languages(Global,&Vector)==S_OK&&Vector!=NULL,"real locale language vector");
    if(Vector){
        IdList((IInspectable*)Vector,1);Check(Vector->lpVtbl->get_Size(Vector,&Size)==S_OK&&Size==1,"bounded default-locale vector size");
        Count=99;Check(Vector->lpVtbl->GetMany(Vector,0,0,NULL,&Count)==S_OK&&Count==0,"NULL buffer with zero capacity");
        Count=99;Check(Vector->lpVtbl->GetMany(Vector,0,0,&Item,&Count)==S_OK&&Count==0&&Item==(HSTRING)(ULONG_PTR)1,"zero capacity leaves element untouched");
        Count=99;Check(Vector->lpVtbl->GetMany(Vector,Size,0,NULL,&Count)==S_OK&&Count==0,"vector end zero capacity");
        Count=99;Check(Vector->lpVtbl->GetMany(Vector,Size+1,1,Items,&Count)==E_BOUNDS&&Count==0,"vector invalid start resets count");
        Count=99;Check(Vector->lpVtbl->GetMany(Vector,0,3,Items,&Count)==S_OK&&Count==Size&&Count<=3,"GetMany publishes actual count");
        for(Index=0;Index<Count;++Index){Check(Items[Index]!=NULL&&WindowsGetStringLen(Items[Index])>0,"GetMany duplicates valid HSTRING");WindowsDeleteString(Items[Index]);}
        Item=NULL;Check(Vector->lpVtbl->GetAt(Vector,Size,&Item)==E_BOUNDS&&Item==NULL,"GetAt bounds clears output");Vector->lpVtbl->Release(Vector);
    }
    Global->lpVtbl->Release(Global);
}

typedef struct MARSHAL_WORK {IStream *Stream;IID Iid;ULONG Kind;HANDLE Done;HRESULT Result;IUnknown *Identity;} MARSHAL_WORK;
static DWORD WINAPI MarshalWorker(PVOID Parameter)
{
    MARSHAL_WORK *Work=Parameter;IUnknown *Object=NULL,*Identity=NULL;HRESULT Initialized;IID *Ids=NULL;ULONG Count=0,Size=0;HSTRING Text=NULL;UIColor Color;IUriRuntimeClass *Combined=NULL;HSTRING Extra;
    Initialized=CoInitializeEx(NULL,COINIT_MULTITHREADED);Work->Result=Initialized;
    if(FAILED(Initialized)){SetEvent(Work->Done);return 1;}
    Work->Result=CoGetInterfaceAndReleaseStream(Work->Stream,&Work->Iid,(void**)&Object);Work->Stream=NULL;
    if(SUCCEEDED(Work->Result)){
        Check(Object->lpVtbl->QueryInterface(Object,&IID_IUnknown,(void**)&Identity)==S_OK&&Identity==Work->Identity,"FTM preserves controlling identity across STA/MTA");if(Identity)Identity->lpVtbl->Release(Identity);
        Work->Result=((IInspectable*)Object)->lpVtbl->GetIids((IInspectable*)Object,&Count,&Ids);Check(Work->Result==S_OK&&Count>0&&Ids!=NULL,"MTA executes real metadata method");TrackedCoFree(Ids);
        if(Work->Kind==1){
            Work->Result=((IUriRuntimeClass*)Object)->lpVtbl->get_AbsoluteUri((IUriRuntimeClass*)Object,&Text);Check(Work->Result==S_OK&&TextEquals(Text,L"https://example.invalid/a/b"),"MTA URI reads native URLMON backend");WindowsDeleteString(Text);
            Extra=Make(L"c");Work->Result=((IUriRuntimeClass*)Object)->lpVtbl->CombineUri((IUriRuntimeClass*)Object,Extra,&Combined);WindowsDeleteString(Extra);Check(Work->Result==S_OK&&Combined!=NULL,"MTA URI combine real backend");if(Combined){Text=NULL;Check(Combined->lpVtbl->get_AbsoluteUri(Combined,&Text)==S_OK&&TextEquals(Text,L"https://example.invalid/a/c"),"MTA combined URL value");WindowsDeleteString(Text);Combined->lpVtbl->Release(Combined);}
        }else if(Work->Kind==2){
            Work->Result=((IVectorView_HSTRING*)Object)->lpVtbl->get_Size((IVectorView_HSTRING*)Object,&Size);Check(Work->Result==S_OK&&Size==1,"MTA vector size method");Text=NULL;Check(((IVectorView_HSTRING*)Object)->lpVtbl->GetAt((IVectorView_HSTRING*)Object,0,&Text)==S_OK&&WindowsGetStringLen(Text)>0,"MTA vector real HSTRING result");WindowsDeleteString(Text);
        }else if(Work->Kind==3){
            Work->Result=((IUISettings3*)Object)->lpVtbl->GetColorValue((IUISettings3*)Object,UIColorType_Background,&Color);Check(Work->Result==S_OK&&Color.A==0xff,"MTA UISettings reads real system color");
        }
        Object->lpVtbl->Release(Object);
    }
    CoUninitialize();SetEvent(Work->Done);return 0;
}
/* Consumes the caller's reference before unmarshal. The real marshaling stream
 * must retain every per-instance payload until the worker finishes its method. */
static void MarshalAndRelease(IUnknown *Object,REFIID Iid,ULONG Kind)
{
    MARSHAL_WORK Work;HANDLE Thread;DWORD Index=99;HRESULT Result;IUnknown *Identity=NULL;
    ZeroMemory(&Work,sizeof(Work));Work.Iid=*Iid;Work.Kind=Kind;Work.Result=E_FAIL;Work.Done=CreateEventW(NULL,TRUE,FALSE,NULL);
    Object->lpVtbl->QueryInterface(Object,&IID_IUnknown,(void**)&Identity);Work.Identity=Identity;if(Identity)Identity->lpVtbl->Release(Identity);
    Result=CoMarshalInterThreadInterfaceInStream(Iid,Object,&Work.Stream);Check(Result==S_OK&&Work.Stream!=NULL,"real STA marshaling stream");Object->lpVtbl->Release(Object);
    if(FAILED(Result)){CloseHandle(Work.Done);return;}
    Thread=CreateThread(NULL,0,MarshalWorker,&Work,0,NULL);Check(Thread!=NULL,"MTA receiver created");
    if(!Thread){Work.Stream->lpVtbl->Release(Work.Stream);CloseHandle(Work.Done);return;}
    Result=CoWaitForMultipleHandles(0,10000,1,&Work.Done,&Index);Check(Result==S_OK&&Index==0,"bounded STA COM pumping wait");if(FAILED(Result))ExitProcess(4);
    Check(WaitForSingleObject(Thread,2000)==WAIT_OBJECT_0&&Work.Result==S_OK,"MTA receiver method completes");
    printf("MARSHAL_METHOD|kind=%lu|hr=0x%08lx|creator_reference_released=1\n",Kind,(ULONG)Work.Result);CloseHandle(Thread);CloseHandle(Work.Done);
}
typedef struct CACHE_WORK {IActivationFactory *Factory;HRESULT Result;} CACHE_WORK;
static DWORD WINAPI CacheWorker(PVOID Parameter)
{
    CACHE_WORK *Work=Parameter;IUnknown *Marshaler=NULL;HRESULT Init=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(Init)){Work->Result=Init;return 1;}
    Work->Result=Work->Factory->lpVtbl->QueryInterface(Work->Factory,&IID_IMarshal,(void**)&Marshaler);if(Marshaler)Marshaler->lpVtbl->Release(Marshaler);CoUninitialize();return 0;
}
static void CacheAndMarshalContracts(void)
{
    IActivationFactory *Object=Factory(4);IUnknown *Marshaler=NULL;CACHE_WORK Work[4];HANDLE Threads[4];ULONG Index;LONG Created,Frees,Cached;HSTRING Name,Text;IInspectable *Instance=NULL;IUISettings3 *Ui=NULL;IUriRuntimeClassFactory *Create=NULL;IUriRuntimeClass *Uri=NULL;IGlobalizationPreferencesStatics *Global=NULL;IVectorView_HSTRING *Vector=NULL;
    FailFtm=TRUE;Check(Object->lpVtbl->QueryInterface(Object,&IID_IMarshal,(void**)&Marshaler)==E_OUTOFMEMORY&&Marshaler==NULL,"cache constructor failure remains retryable");FailFtm=FALSE;
    Created=FtmCreated;Frees=FtmFreed;Cached=LiveCachedMarshalers;BarrierCount=0;FtmBarrierTarget=4;FtmBarrier=CreateEventW(NULL,TRUE,FALSE,NULL);
    for(Index=0;Index<4;++Index){Work[Index].Factory=Object;Work[Index].Result=E_FAIL;Threads[Index]=CreateThread(NULL,0,CacheWorker,&Work[Index],0,NULL);Check(Threads[Index]!=NULL,"concurrent immutable factory QI thread");}
    for(Index=0;Index<4;++Index){Check(WaitForSingleObject(Threads[Index],10000)==WAIT_OBJECT_0&&Work[Index].Result==S_OK,"concurrent FTM constructor succeeds");CloseHandle(Threads[Index]);}
    CloseHandle(FtmBarrier);FtmBarrier=NULL;
    Check(FtmCreated==Created+4&&FtmFreed==Frees+3&&LiveCachedMarshalers==Cached+1,"one cache winner retains native FTM, three losing constructors release");Object->lpVtbl->Release(Object);
    for(Index=0;Index<ARRAYSIZE(Names);++Index){Object=Factory(Index);if(Object)MarshalAndRelease((IUnknown*)Object,&IID_IActivationFactory,0);}
    Name=Make(Names[1]);Check(RoActivateInstance(Name,&Instance)==S_OK&&Instance!=NULL,"UISettings instance for marshaling");WindowsDeleteString(Name);if(Instance){Check(Instance->lpVtbl->QueryInterface(Instance,&IID_IUISettings3,(void**)&Ui)==S_OK,"UISettings3 for marshaling");Instance->lpVtbl->Release(Instance);if(Ui)MarshalAndRelease((IUnknown*)Ui,&IID_IUISettings3,3);}
    KxCompQueryBuiltinFactory(KxRuntimeUri,&IID_IUriRuntimeClassFactory,(void**)&Create);Text=Make(L"https://example.invalid/a/b");Check(Create->lpVtbl->CreateUri(Create,Text,&Uri)==S_OK&&Uri!=NULL,"URI instance for marshaling");WindowsDeleteString(Text);Create->lpVtbl->Release(Create);if(Uri)MarshalAndRelease((IUnknown*)Uri,&IID_IUriRuntimeClass,1);
    KxCompQueryBuiltinFactory(KxRuntimeGlobalizationPreferences,&IID_IGlobalizationPreferencesStatics,(void**)&Global);Check(Global->lpVtbl->get_Languages(Global,&Vector)==S_OK&&Vector!=NULL,"vector for marshaling");Global->lpVtbl->Release(Global);if(Vector)MarshalAndRelease((IUnknown*)Vector,&TypedVector,2);
    Check(LiveInstanceMarshalers==0,"stream/receiver releases final URI/vector payload and FTM");
}

int __cdecl main(void)
{
    HSTRING Name;IActivationFactory *Object=NULL;
    InitializeCriticalSection(&MemoryLock);
    setvbuf(stdout,NULL,_IONBF,0);
    Check(!GetModuleHandleW(L"KexDll.dll")&&!GetModuleHandleW(L"KxCom.dll"),"source fixture has no injected Kex");
    Name=Make(Names[0]);Check(RoGetActivationFactory(Name,&IID_IActivationFactory,(void**)&Object)==CO_E_NOTINITIALIZED&&Object==NULL,"activation requires COM initialization");WindowsDeleteString(Name);
    Check(SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED)),"candidate delegates real STA initialization");
    NativeOpen();StringComparisonContracts();NativeUriReference();ClassContracts();UriContracts();VectorContracts();CacheAndMarshalContracts();NativeClose();
    Check(OwnedMemory==0&&MemoryRecords==NULL,"all owned source allocations released");Check(LiveInstanceMarshalers==0,"all per-instance native marshalers released");
    Check(LiveCachedMarshalers==ARRAYSIZE(Names)+1,"only exercised process-lifetime singleton caches remain");
    printf("WINRT_SUMMARY|checks=%ld|failures=%ld|owned_allocations=%ld|instance_marshalers=%ld|cached_marshalers=%ld|ftm_created=%ld|ftm_freed=%ld\n",Checks,Failures,OwnedMemory,LiveInstanceMarshalers,LiveCachedMarshalers,FtmCreated,FtmFreed);
    RoUninitialize();DeleteCriticalSection(&MemoryLock);return Failures?1:0;
}
