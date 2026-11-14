#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *QUERYPROCESS)(HANDLE,ULONG,PVOID,ULONG,PULONG);
typedef NTSTATUS (NTAPI *QUERYSYSTEM)(ULONG,PVOID,ULONG,PULONG);
typedef NTSTATUS (NTAPI *QUERYOBJECT)(HANDLE,ULONG,PVOID,ULONG,PULONG);
typedef struct { NTSTATUS ExitStatus; PVOID Peb; ULONG_PTR Affinity; LONG Priority;
    ULONG_PTR Pid; ULONG_PTR Parent; } BASIC;
typedef struct { DWORD Magic,Pid; ULONG_PTR Owned[5]; } SHARED;
typedef struct { ULONG Fields[12]; LARGE_INTEGER Created; } OBJECTBASIC;
typedef struct { PVOID Object; ULONG_PTR Pid,Handle; ULONG Access;
    USHORT Backtrace,Type; ULONG Attributes,Reserved; } SYSENTRY;
typedef struct { ULONG_PTR Count,Reserved; SYSENTRY Entries[1]; } SYSTABLE;
static QUERYPROCESS Q;
static QUERYOBJECT O;
#ifdef HANDLE_TABLE_FALLBACK_TEST
LONG NTAPI Ext_NtQueryInformationProcess(HANDLE,ULONG,PVOID,ULONG,PULONG);
#endif
static HANDLE Owned[5];
static ULONG_PTR ChildOwned[5];

static int IsNative(FARPROC p, HMODULE native)
{
    HMODULE owner=NULL;
    union { FARPROC function; LPCWSTR address; } value;
    value.function=p;
    return p && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,value.address,&owner) && owner==native;
}

static void Query(const char *label,HANDLE process,ULONG length,int special)
{
    union { ULONG_PTR align; BYTE bytes[8224]; } storage;
    union { ULONG align; BYTE bytes[16]; } lengthStore;
    PVOID output=storage.bytes+16;
    PULONG returned=(PULONG)(lengthStore.bytes+4);
    ULONG ret=0xc0dec0de,exception=0,i,known=0,unique=1,ascending=1;
    ULONG changed=0,tailChanged=0,lastError; NTSTATUS status=(NTSTATUS)0x7a716263;
    FillMemory(storage.bytes,sizeof(storage.bytes),0xa5);
    FillMemory(lengthStore.bytes,sizeof(lengthStore.bytes),0xa5);
    *returned=ret;
    if(special==1) output=NULL;
    if(special==2) output=storage.bytes+17;
    if(special==3) returned=NULL;
    if(special==4) { returned=(PULONG)(lengthStore.bytes+5);CopyMemory(returned,&ret,sizeof(ret)); }
    if(special==5) returned=(PULONG)(ULONG_PTR)1;
    if(special==6) output=storage.bytes+20;
    SetLastError(0x6b145239);
    __try { status=Q(process,58,output,length,returned); }
    __except(EXCEPTION_EXECUTE_HANDLER) { exception=GetExceptionCode(); }
    lastError=GetLastError();
    if(returned && special!=5) CopyMemory(&ret,returned,sizeof(ret));
    for(i=0;i<sizeof(storage.bytes);++i) {
        if(storage.bytes[i]!=0xa5) {
            ULONG start=special==2?17:(special==6?20:16);
            ++changed;
            if(i<start || i>=start+length) ++tailChanged;
        }
    }
    if(status>=0 && output && ret<=length && ret<=8192 && special!=2) {
        ULONG n=ret/(ULONG)sizeof(ULONG),j,k;
        ULONG *handles=(ULONG*)output;
        for(i=0;i<n;++i) {
            if(i && (ULONG_PTR)handles[i]<(ULONG_PTR)handles[i-1]) ascending=0;
            for(j=0;j<i;++j) if(handles[i]==handles[j]) unique=0;
            for(k=0;k<5;++k) {
                ULONG_PTR expected=label[0]=='s'?(ULONG_PTR)Owned[k]:ChildOwned[k];
                if(handles[i]==expected) known|=1<<k;
            }
        }
    }
    printf("QUERY|%s|length=%lu|special=%d|status=0x%08lx|exception=0x%08lx|returned=0x%08lx|changed=%lu|outside_capacity=%lu|known=0x%02lx|unique=%lu|ascending=%lu|last_error=0x%08lx\n",
        label,length,special,(ULONG)status,exception,ret,changed,tailChanged,known,unique,ascending,lastError);
}

static void Boundary(HANDLE process,const char *label)
{
    SYSTEM_INFO info;BYTE *memory;DWORD old,i;ULONG length,returned;NTSTATUS s;
    GetSystemInfo(&info);
    memory=(BYTE*)VirtualAlloc(NULL,info.dwPageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!memory) return;
    VirtualProtect(memory+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old);
    for(i=0;i<3;++i) {
        length=i==0?4:(i==1?5:8);returned=0xc0dec0de;
        s=Q(process,58,memory+info.dwPageSize-4,length,&returned);
        printf("BOUNDARY|%s|length=%lu|status=0x%08lx|returned=0x%08lx\n",label,length,(ULONG)s,returned);
    }
    VirtualProtect(memory,info.dwPageSize,PAGE_READONLY,&old);returned=0xc0dec0de;
    s=Q(process,58,memory,4,&returned);
    printf("READONLY|%s|status=0x%08lx|returned=0x%08lx\n",label,(ULONG)s,returned);
    VirtualFree(memory,0,MEM_RELEASE);
}

static void Snapshot(QUERYSYSTEM query)
{
    ULONG capacity=65536,returned=0,attempt,known=0;SYSTABLE *table=NULL;
    NTSTATUS status=(NTSTATUS)0xc000009a;ULONG_PTR i;DWORD pid=GetCurrentProcessId();
    for(attempt=0;attempt<12;++attempt) {
        table=(SYSTABLE*)HeapAlloc(GetProcessHeap(),0,capacity);if(!table) break;
        status=query(64,table,capacity,&returned);
        if(status!=(NTSTATUS)0xc0000004) break;
        HeapFree(GetProcessHeap(),0,table);table=NULL;
        if(capacity>=64*1024*1024 || returned>64*1024*1024) break;
        capacity=returned>capacity*2?returned:capacity*2;
    }
    if(status>=0 && table && returned>=FIELD_OFFSET(SYSTABLE,Entries) &&
        returned<=capacity && table->Count<=(returned-FIELD_OFFSET(SYSTABLE,Entries))/sizeof(SYSENTRY)) {
        for(i=0;i<table->Count;++i) if(table->Entries[i].Pid==pid) {
            ULONG k;for(k=0;k<5;++k) if(table->Entries[i].Handle==(ULONG_PTR)Owned[k]) known|=1<<k;
        }
        printf("SNAPSHOT64|status=0x%08lx|returned=%lu|entry_bytes=%lu|own_known=0x%02lx|layout_valid=1|no_privilege_changed=1\n",
            (ULONG)status,returned,(ULONG)sizeof(SYSENTRY),known);
    } else printf("SNAPSHOT64|status=0x%08lx|returned=%lu|layout_valid=0|no_privilege_changed=1\n",(ULONG)status,returned);
    if(table)HeapFree(GetProcessHeap(),0,table);
}

static void Matrix(const char *label,HANDLE process)
{
    ULONG lengths[]={0,1,2,3,4,5,sizeof(HANDLE)-1,sizeof(HANDLE),sizeof(HANDLE)+1,
        2*sizeof(HANDLE)-1,2*sizeof(HANDLE),3*sizeof(HANDLE),4096};
    BASIC b; OBJECTBASIC basic; ULONG i,r=0xc0dec0de,ret=0xc0dec0de;
    NTSTATUS s=Q(process,0,&b,sizeof(b),&r),o;
    ZeroMemory(&basic,sizeof(basic)); o=O(process,0,&basic,sizeof(basic),&ret);
    printf("ACCESS|%s|basic_status=0x%08lx|basic_returned=0x%08lx|object_status=0x%08lx|granted=0x%08lx\n",
        label,(ULONG)s,r,(ULONG)o,o>=0?basic.Fields[1]:0);
    for(i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i) Query(label,process,lengths[i],0);
    for(i=1;i<=6;++i) Query(label,process,sizeof(HANDLE),i);
    Query(label,process,0,1); Query(label,process,3,1);
}

static int Child(int argc,wchar_t **argv)
{
    HANDLE stop,ready,mapping; SHARED *shared; DWORD i;
    if(argc!=5) return 21;
    stop=(HANDLE)(ULONG_PTR)_wcstoui64(argv[2],NULL,16);
    ready=(HANDLE)(ULONG_PTR)_wcstoui64(argv[3],NULL,16);
    mapping=(HANDLE)(ULONG_PTR)_wcstoui64(argv[4],NULL,16);
    shared=(SHARED*)MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(SHARED));
    if(!shared) return 22;
    for(i=0;i<3;++i) Owned[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
    for(i=3;i<5;++i) Owned[i]=CreateJobObjectW(NULL,NULL);
    for(i=0;i<5;++i) if(!Owned[i]) return 23;
    shared->Pid=GetCurrentProcessId();
    for(i=0;i<5;++i) shared->Owned[i]=(ULONG_PTR)Owned[i];
    shared->Magic=0x48544231;
    if(!SetEvent(ready)) return 24;
    if(WaitForSingleObject(stop,30000)!=WAIT_OBJECT_0) return 25;
    for(i=0;i<5;++i) CloseHandle(Owned[i]);
    UnmapViewOfFile(shared); CloseHandle(mapping); CloseHandle(ready);CloseHandle(stop);
    return 259; /* Deliberately the STILL_ACTIVE/STATUS_PENDING numeric value. */
}

int wmain(int argc,wchar_t **argv)
{
    HMODULE native=GetModuleHandleW(L"ntdll.dll");
#ifdef HANDLE_TABLE_KEX_TEST
    HMODULE kex=GetModuleHandleW(L"KexDll.dll");
#endif
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    HANDLE stop=NULL,ready=NULL,mapping=NULL,h; SHARED *shared=NULL;
    STARTUPINFOW si; PROCESS_INFORMATION pi; wchar_t exe[MAX_PATH],command[2048];
    const DWORD rights[]={0,PROCESS_QUERY_LIMITED_INFORMATION,PROCESS_QUERY_INFORMATION,
        PROCESS_DUP_HANDLE,SYNCHRONIZE,PROCESS_QUERY_INFORMATION|PROCESS_DUP_HANDLE};
    const char *labels[]={"child-none","child-limited","child-query","child-dup","child-sync","child-query-dup"};
    DWORD i,exitcode=99; ULONG required=0; NTSTATUS systemStatus;
    QUERYSYSTEM systemQuery;
    if(argc>1 && wcscmp(argv[1],L"--child")==0) return Child(argc,argv);
    if(argc!=1) return 2;
#ifndef HANDLE_TABLE_KEX_TEST
    if(GetModuleHandleW(L"KexDll.dll") || GetModuleHandleW(L"KxNt.dll") || GetModuleHandleW(L"KxBase.dll")) return 3;
#else
    if(!kex) return 3;
#endif
    Q=(QUERYPROCESS)GetProcAddress(native,"NtQueryInformationProcess");
    O=(QUERYOBJECT)GetProcAddress(native,"NtQueryObject");
    systemQuery=(QUERYSYSTEM)GetProcAddress(native,"NtQuerySystemInformation");
#ifndef HANDLE_TABLE_KEX_TEST
    if(!IsNative((FARPROC)Q,native)||!IsNative((FARPROC)O,native)||!IsNative((FARPROC)systemQuery,native)) return 4;
    printf("BEGIN|bits=%lu|native_owner=1|kex_absent=1|debugger=%d\n",(ULONG)(sizeof(void*)*8),IsDebuggerPresent());
#else
    Q=(QUERYPROCESS)GetProcAddress(kex,"Ext_NtQueryInformationProcess");
    if(!IsNative((FARPROC)Q,kex)||!O||!systemQuery) return 4;
    printf("BEGIN|bits=%lu|kex_export_owner_verified=1|query_native_owner=%d|system_native_owner=%d|debugger=%d\n",
        (ULONG)(sizeof(void*)*8),IsNative((FARPROC)O,native),IsNative((FARPROC)systemQuery,native),IsDebuggerPresent());
#endif
#ifdef HANDLE_TABLE_FALLBACK_TEST
    Q=Ext_NtQueryInformationProcess;
    printf("FALLBACK_TEST|production_body=1|only_class58_availability_forced=1|real_system64_backend=1\n");
#endif
    for(i=0;i<3;++i) Owned[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
    for(i=3;i<5;++i) Owned[i]=CreateJobObjectW(NULL,NULL);
    for(i=0;i<5;++i) if(!Owned[i]) return 5;
    stop=CreateEventW(&sa,TRUE,FALSE,NULL); ready=CreateEventW(&sa,TRUE,FALSE,NULL);
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(SHARED),NULL);
    if(!stop||!ready||!mapping) return 6;
    shared=(SHARED*)MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(SHARED));
    if(!shared) return 7;
    ZeroMemory(shared,sizeof(*shared));
    ZeroMemory(&si,sizeof(si));si.cb=sizeof(si); ZeroMemory(&pi,sizeof(pi));
    if(!GetModuleFileNameW(NULL,exe,MAX_PATH)) return 8;
    _snwprintf_s(command,2048,_TRUNCATE,L"\"%s\" --child %I64x %I64x %I64x",exe,
        (unsigned __int64)(ULONG_PTR)stop,(unsigned __int64)(ULONG_PTR)ready,(unsigned __int64)(ULONG_PTR)mapping);
    if(!CreateProcessW(exe,command,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) return 9;
    if(WaitForSingleObject(ready,10000)!=WAIT_OBJECT_0 || shared->Magic!=0x48544231 || shared->Pid!=pi.dwProcessId) return 10;
    for(i=0;i<5;++i) ChildOwned[i]=shared->Owned[i];
    printf("OWNED|self_events=3|self_jobs=2|child_events=3|child_jobs=2|child_live=1\n");
    Matrix("self-pseudo",GetCurrentProcess());
    Matrix("child-full",pi.hProcess);
    for(i=0;i<sizeof(rights)/sizeof(rights[0]);++i) {
        h=OpenProcess(rights[i],FALSE,pi.dwProcessId);
        printf("OPEN|%s|rights=0x%08lx|success=%d|error=%lu\n",labels[i],rights[i],h!=NULL,h?0:GetLastError());
        if(h) { Matrix(labels[i],h);CloseHandle(h); }
    }
    Matrix("invalid",(HANDLE)(ULONG_PTR)0x12345678);
    Matrix("wrong-event",Owned[0]);
    Matrix("wrong-thread-pseudo",GetCurrentThread());
    Boundary(GetCurrentProcess(),"self");Boundary((HANDLE)(ULONG_PTR)0x12345678,"invalid");
    systemStatus=systemQuery(64,NULL,0,&required);
    printf("SYSTEM64|status=0x%08lx|required=%lu|no_privilege_changed=1\n",(ULONG)systemStatus,required);
    Snapshot(systemQuery);
    if(!SetEvent(stop)||WaitForSingleObject(pi.hProcess,10000)!=WAIT_OBJECT_0) return 11;
    GetExitCodeProcess(pi.hProcess,&exitcode);
    printf("CHILD_EXIT|code=%lu\n",exitcode);
    Matrix("child-exited",pi.hProcess);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    UnmapViewOfFile(shared);CloseHandle(mapping);CloseHandle(ready);CloseHandle(stop);
    for(i=0;i<5;++i) CloseHandle(Owned[i]);
    printf("COMPLETE|inventory_only=1|no_privilege_or_system_changes=1\n");
    return exitcode==259?0:12;
}
