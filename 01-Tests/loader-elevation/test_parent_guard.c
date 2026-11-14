/* Only controlled API doubles execute; no real process/token is opened. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32) && !defined(LOADER_TEST_PORTABLE)
#define _WIN32_WINNT 0x0601
#include <windows.h>
#else
typedef void *HANDLE, *PVOID, *PSID;
typedef uint32_t DWORD, ULONG;
typedef unsigned char BYTE, UCHAR, BOOLEAN;
typedef int BOOL;
typedef struct { PSID Sid; DWORD Attributes; } SID_AND_ATTRIBUTES;
typedef struct { SID_AND_ATTRIBUTES User; } TOKEN_USER;
typedef struct { SID_AND_ATTRIBUTES Label; } TOKEN_MANDATORY_LABEL;
typedef struct { DWORD TokenIsElevated; } TOKEN_ELEVATION;
typedef enum { TokenUser=1, TokenSessionId=12, TokenElevation=20, TokenIntegrityLevel=25 } TOKEN_INFORMATION_CLASS;
#define TRUE 1
#define FALSE 0
#define TOKEN_QUERY 8
#define SECURITY_MAX_SID_SIZE 68
#define SECURITY_MANDATORY_HIGH_RID 0x3000
#endif
#define STATIC static
#define IN

typedef struct {
    ULONG Identity, Rid;
    UCHAR Count;
    BOOL Valid;
} FAKE_SID;
typedef struct {
    FAKE_SID User, Label;
    DWORD Session, Elevated;
} FAKE_TOKEN;

static FAKE_TOKEN Tokens[2];
static DWORD LastErrorValue;
static int OpenCalls, QueryCalls, CloseCalls, Outstanding, FailOpen, FailQuery, BadLengthQuery;
static int SeenCurrent, SeenParent, BadCall, Checks;
static BOOL Live[2];

#define CHECK(Condition) { ++Checks; if(!(Condition)) { \
    fprintf(stderr,"line %d: %s\n",__LINE__,#Condition); return 1; } }

static HANDLE MockCurrentProcess(void) { return (HANDLE)(uintptr_t)1; }
static DWORD MockGetLastError(void) { return LastErrorValue; }
static void MockSetLastError(DWORD Value) { LastErrorValue=Value; }
static BOOL MockOpenProcessToken(HANDLE Process,DWORD Access,HANDLE *Token) {
    int Index;
    ++OpenCalls;
    if(Access!=TOKEN_QUERY || (Process!=(HANDLE)(uintptr_t)1 && Process!=(HANDLE)(uintptr_t)2)) ++BadCall;
    if(OpenCalls==FailOpen) { LastErrorValue=5; return FALSE; }
    Index=Process==(HANDLE)(uintptr_t)1?0:1;
    if(Index==0)++SeenCurrent;else ++SeenParent;
    if(Live[Index]) ++BadCall;
    Live[Index]=TRUE; ++Outstanding; *Token=(HANDLE)(uintptr_t)(10+Index);
    LastErrorValue=0xaaaa; return TRUE;
}
static BOOL MockGetTokenInformation(HANDLE Token,TOKEN_INFORMATION_CLASS Class,
    PVOID Buffer,DWORD Length,DWORD *Required) {
    int Index=(int)(uintptr_t)Token-10;
    ++QueryCalls;
    if(Index<0 || Index>1 || !Live[Index]) { ++BadCall; return FALSE; }
    if(QueryCalls==FailQuery) { LastErrorValue=5; return FALSE; }
    if(Class==TokenUser) {
        if(Length<sizeof(TOKEN_USER)) { ++BadCall; return FALSE; }
        ((TOKEN_USER*)Buffer)->User.Sid=&Tokens[Index].User;*Required=sizeof(TOKEN_USER);
    } else if(Class==TokenElevation) {
        if(Length!=sizeof(TOKEN_ELEVATION))++BadCall;
        ((TOKEN_ELEVATION*)Buffer)->TokenIsElevated=Tokens[Index].Elevated;*Required=sizeof(TOKEN_ELEVATION);
    } else if(Class==TokenSessionId) {
        if(Length!=sizeof(ULONG))++BadCall;
        *(ULONG*)Buffer=Tokens[Index].Session;*Required=sizeof(ULONG);
    } else if(Class==TokenIntegrityLevel) {
        if(Length<sizeof(TOKEN_MANDATORY_LABEL)) { ++BadCall; return FALSE; }
        ((TOKEN_MANDATORY_LABEL*)Buffer)->Label.Sid=&Tokens[Index].Label;*Required=sizeof(TOKEN_MANDATORY_LABEL);
    } else { ++BadCall;return FALSE; }
    if(QueryCalls==BadLengthQuery)*Required=Length+1;
    LastErrorValue=0xbbbb;return TRUE;
}
static BOOL MockValidSid(PSID Sid) { return Sid && ((FAKE_SID*)Sid)->Valid; }
static UCHAR *MockSubAuthorityCount(PSID Sid) { return &((FAKE_SID*)Sid)->Count; }
static ULONG *MockSubAuthority(PSID Sid,DWORD Index) {
    if(Index!=0)++BadCall;
    return &((FAKE_SID*)Sid)->Rid;
}
static BOOL MockEqualSid(PSID Left,PSID Right) {
    return ((FAKE_SID*)Left)->Identity==((FAKE_SID*)Right)->Identity;
}
static BOOL MockCloseHandle(HANDLE Token) {
    int Index=(int)(uintptr_t)Token-10;
    if(Index<0 || Index>1 || !Live[Index]) { ++BadCall; return FALSE; }
    Live[Index]=FALSE;--Outstanding;++CloseCalls;LastErrorValue=0xcccc;return TRUE;
}

#define GetCurrentProcess MockCurrentProcess
#define GetLastError MockGetLastError
#define SetLastError MockSetLastError
#define OpenProcessToken MockOpenProcessToken
#define GetTokenInformation MockGetTokenInformation
#define IsValidSid MockValidSid
#define GetSidSubAuthorityCount MockSubAuthorityCount
#define GetSidSubAuthority MockSubAuthority
#define EqualSid MockEqualSid
#define CloseHandle MockCloseHandle
#include "parentguard.h"

static void Reset(void) {
    int Index;
    memset(Tokens,0,sizeof(Tokens));memset(Live,0,sizeof(Live));
    for(Index=0;Index<2;++Index) {
        Tokens[Index].User.Identity=42;Tokens[Index].User.Valid=TRUE;
        Tokens[Index].Label.Valid=TRUE;Tokens[Index].Label.Count=1;Tokens[Index].Label.Rid=0x2000;
        Tokens[Index].Session=1;
    }
    LastErrorValue=0x1357;OpenCalls=QueryCalls=CloseCalls=Outstanding=0;
    FailOpen=FailQuery=BadLengthQuery=SeenCurrent=SeenParent=BadCall=0;
}
static int Run(BOOL Expected) {
    BOOL Compatible=VklCanUseParentProcess((HANDLE)(uintptr_t)2);
    CHECK(Compatible==Expected);CHECK(Outstanding==0);CHECK(BadCall==0);
    CHECK(LastErrorValue==0x1357);CHECK(CloseCalls==SeenCurrent+SeenParent);
    return 0;
}
int main(void) {
    int Index;
    Reset();CHECK(Run(TRUE)==0);CHECK(OpenCalls==2);CHECK(QueryCalls==8);
    /* Actual old behavior would have inherited the parent's Medium token. */
    Reset();Tokens[0].Elevated=1;Tokens[0].Label.Rid=0x3000;
    CHECK(Run(FALSE)==0);CHECK(SeenParent==0);CHECK(Tokens[0].Label.Rid!=Tokens[1].Label.Rid);
    /* Even High -> High preserves the exact creator token/job, not a proxy. */
    Reset();Tokens[0].Elevated=Tokens[1].Elevated=1;
    Tokens[0].Label.Rid=Tokens[1].Label.Rid=0x3000;
    CHECK(Run(FALSE)==0);CHECK(SeenParent==0);
    Reset();Tokens[0].Elevated=1;CHECK(Run(FALSE)==0);CHECK(SeenParent==0);
    Reset();Tokens[0].Label.Rid=0x3000;CHECK(Run(FALSE)==0);CHECK(SeenParent==0);
    Reset();Tokens[0].Label.Rid=0x4000;CHECK(Run(FALSE)==0);CHECK(SeenParent==0);
    Reset();Tokens[1].Elevated=1;CHECK(Run(FALSE)==0);
    Reset();Tokens[1].Label.Rid=0x3000;CHECK(Run(FALSE)==0);
    Reset();Tokens[1].User.Identity=43;CHECK(Run(FALSE)==0);
    Reset();Tokens[1].Session=2;CHECK(Run(FALSE)==0);
    Reset();Tokens[0].User.Valid=FALSE;CHECK(Run(FALSE)==0);
    Reset();Tokens[1].Label.Valid=FALSE;CHECK(Run(FALSE)==0);
    Reset();Tokens[0].Label.Count=0;CHECK(Run(FALSE)==0);
    Reset();Tokens[1].Label.Count=0;CHECK(Run(FALSE)==0);
    for(Index=1;Index<=2;++Index){Reset();FailOpen=Index;CHECK(Run(FALSE)==0);}
    for(Index=1;Index<=8;++Index){Reset();FailQuery=Index;CHECK(Run(FALSE)==0);}
    for(Index=1;Index<=8;++Index){Reset();BadLengthQuery=Index;CHECK(Run(FALSE)==0);}
    printf("Actual loader parent guard: %d checks passed; no real token/process calls.\n",Checks);
    return 0;
}
