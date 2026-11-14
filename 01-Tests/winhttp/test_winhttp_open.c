/* The production WinHTTP fallback, with controlled API doubles and no network. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include <stdint.h>
#define STATIC static
#define KXNETAPI
#ifdef _MSC_VER
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define IN
#define TRUE 1
#define FALSE 0
#define ERROR_INVALID_PARAMETER 87
#define WINHTTP_FLAG_ASYNC 0x10000000UL
#define WINHTTP_ACCESS_TYPE_NO_PROXY 1
typedef uint32_t ULONG;
typedef int BOOL;
typedef const wchar_t *PCWSTR;
typedef void *HINTERNET;
typedef struct { BOOL fAutoDetect; wchar_t *lpszAutoConfigUrl,*lpszProxy,*lpszProxyBypass; } WINHTTP_CURRENT_USER_IE_PROXY_CONFIG;
typedef struct { ULONG dwAccessType; wchar_t *lpszProxy,*lpszProxyBypass; } WINHTTP_PROXY_INFO;
static ULONG Error,FirstError,SecondError,LastAccess,ExpectedFlags;
static HINTERNET FirstHandle,SecondHandle;
static unsigned OpenCalls,IeCalls,DefaultCalls,Allocated,Freed;
static BOOL IeOk,DefaultOk,AutoDetect,FailFree;
static void *FailedFreeMemory;
static const wchar_t *Strings[5];
static PCWSTR ExpectedAgent,ExpectedProxy,ExpectedBypass;
static ULONG WINAPI GetLastError(void) { return Error; }
static void WINAPI SetLastError(ULONG error) { Error=error; }
static wchar_t *Copy(const wchar_t *source) {
    wchar_t *copy; size_t bytes;
    if (!source) return NULL;
    bytes=(wcslen(source)+1)*sizeof(wchar_t);
    copy=(wchar_t*)malloc(bytes); assert(copy); memcpy(copy,source,bytes); ++Allocated; return copy;
}
static void * WINAPI GlobalFree(void *memory) {
    assert(memory); ++Freed; Error=0xAAAA;
    if (FailFree) { FailedFreeMemory=memory; return memory; }
    free(memory); assert(Allocated); --Allocated;
    return NULL;
}
static HINTERNET WINAPI WinHttpOpen(PCWSTR agent,ULONG access,PCWSTR proxy,PCWSTR bypass,ULONG flags) {
    ++OpenCalls; LastAccess=access;
    assert(agent==ExpectedAgent && proxy==ExpectedProxy && bypass==ExpectedBypass && flags==ExpectedFlags);
    if(OpenCalls==1) { assert(!IeCalls && !DefaultCalls); Error=FirstError; return FirstHandle; }
    assert(OpenCalls==2 && access==1 && !Allocated && IeCalls==1 && DefaultCalls==1);
    Error=SecondError; return SecondHandle;
}
static BOOL WINAPI WinHttpGetIEProxyConfigForCurrentUser(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *out) {
    ++IeCalls; assert(OpenCalls==1 && !DefaultCalls);
    out->fAutoDetect=AutoDetect; out->lpszAutoConfigUrl=Copy(Strings[0]);
    out->lpszProxy=Copy(Strings[1]); out->lpszProxyBypass=Copy(Strings[2]);
    Error=0xBBBB; return IeOk;
}
static ULONG DefaultAccess;
static BOOL WINAPI WinHttpGetDefaultProxyConfiguration(WINHTTP_PROXY_INFO *out) {
    ++DefaultCalls; assert(OpenCalls==1 && IeCalls==1);
    out->dwAccessType=DefaultAccess;out->lpszProxy=Copy(Strings[3]);out->lpszProxyBypass=Copy(Strings[4]);
    Error=0xCCCC; return DefaultOk;
}

/* PRODUCTION */

static void Reset(void) {
    unsigned i;
    assert(!Allocated);
    Error=0x1234; FirstError=87; SecondError=0x5678;
    FirstHandle=NULL;SecondHandle=(HINTERNET)(uintptr_t)0x2222;
    OpenCalls=IeCalls=DefaultCalls=Freed=0;
    IeOk=DefaultOk=TRUE;AutoDetect=FailFree=FALSE;DefaultAccess=1;FailedFreeMemory=NULL;
    for(i=0;i<5;++i) Strings[i]=NULL;
    ExpectedAgent=L"unchanged agent";
    ExpectedProxy=ExpectedBypass=NULL;ExpectedFlags=WINHTTP_FLAG_ASYNC;
}
static HINTERNET Run(ULONG access) { return Ext_WinHttpOpen(ExpectedAgent,access,ExpectedProxy,ExpectedBypass,ExpectedFlags); }
static void Refused(void) {
    assert(Run(4)==NULL && Error==87 && OpenCalls==1 && !Allocated);
}
int main(void) {
    unsigned i;
    Reset(); assert(Run(4)==SecondHandle && OpenCalls==2 && LastAccess==1 && Error==SecondError && !Allocated);
    Reset(); ExpectedFlags=0;ExpectedAgent=NULL;
    assert(Run(4)==SecondHandle && OpenCalls==2 && Error==SecondError);
    Reset(); FirstHandle=(HINTERNET)(uintptr_t)0x1111; FirstError=0x9876;
    assert(Run(4)==FirstHandle && Error==FirstError && OpenCalls==1 && !IeCalls && !DefaultCalls);
    Reset(); FirstError=8;assert(Run(4)==NULL && Error==8 && !IeCalls);
    for(i=0;i<5;++i) if(i!=4) {
        Reset();assert(Run(i)==NULL && Error==87 && OpenCalls==1 && !IeCalls);
    }
    Reset();ExpectedProxy=L"";Refused();assert(!IeCalls);
    Reset();ExpectedBypass=L"";Refused();assert(!IeCalls);
    Reset();ExpectedFlags=0x30000000;Refused();assert(!IeCalls); /* Never weaken SECURE_DEFAULTS. */
    Reset();ExpectedFlags=1;Refused();assert(!IeCalls);
    Reset();AutoDetect=TRUE;Refused();
    for(i=0;i<5;++i) { Reset();Strings[i]=L"configured";Refused();assert(Freed==1); }
    Reset();DefaultAccess=0;Refused();
    Reset();DefaultAccess=3;Refused();
    Reset();IeOk=FALSE;Strings[0]=L"partial";Strings[1]=L"partial";Strings[2]=L"partial";
    Refused();assert(!DefaultCalls && Freed==3);
    Reset();DefaultOk=FALSE;for(i=0;i<5;++i)Strings[i]=L"partial";
    Refused();assert(Freed==5);
    Reset();for(i=0;i<5;++i)Strings[i]=L"";
    assert(Run(4)==SecondHandle && OpenCalls==2 && Freed==5 && !Allocated && Error==SecondError);
    Reset();SecondHandle=NULL;SecondError=12029;
    assert(Run(4)==NULL && OpenCalls==2 && Error==12029);
    Reset();FailFree=TRUE;Strings[0]=L"";
    assert(Run(4)==NULL && Error==87 && OpenCalls==1 && Freed==1 && Allocated==1);
    assert(FailedFreeMemory); free(FailedFreeMemory); --Allocated; /* Release the injected failure outside the wrapper. */
    puts("WinHTTP direct fallback: native precedence, strict configuration/flag gates, cleanup and real errors passed.");
    return 0;
}
