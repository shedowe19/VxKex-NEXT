#include "service_test_shim.h"
#include <assert.h>
#include <stdio.h>
static TEST_KEX_PROCESS_DATA Data;
static DWORD Error,Calls,Logs;
static NTSTATUS InitStatus;
static BOOLEAN ReturnNull,Creates;
static ULONG Result;
DWORD WINAPI MockGetLastError(VOID){return Error;}
VOID WINAPI MockSetLastError(DWORD value){Error=value;}
NTSTATUS WINAPI MockKexDataInitialize(TEST_KEX_PROCESS_DATA **out){assert(out&&!*out);*out=ReturnNull?NULL:&Data;Error=901;return InitStatus;}
ULONG KXCFGAPI KxCfgTryInheritServiceParameters(PCWSTR name,const KEX_IFEO_PARAMETERS *parameters,PBOOLEAN created){assert(!wcscmp(name,L"VendorSvc")&&parameters==&Data.IfeoParameters);++Calls;*created=Creates;Error=902;return Result;}
VOID MockVxlWriteLog(HANDLE log,PCWSTR component,ULONG level,PCWSTR format,...){assert(log==Data.LogHandle&&component&&level==LogSeverityInformation&&format&&Creates);++Logs;Error=903;}
/* PRODUCTION_BRIDGE_FUNCTION */
static void Reset(void){memset(&Data,0,sizeof(Data));Data.Flags=KEXDATA_FLAG_IFEO_OPTIONS_PRESENT;Data.LogHandle=(HANDLE)(ULONG_PTR)42;Data.IfeoParameters.WinVerSpoof=WinVerSpoofWin10;Data.IfeoParameters.DisableAppSpecific=1;Data.IfeoParameters.StrongVersionSpoof=2;Data.IfeoParameters.DisableConsoleEnhancements=1;Data.IfeoParameters.TlsForceEnabledProtocols=0x800;Data.IfeoParameters.TlsForceDisabledProtocols=0x300;Error=333;Calls=Logs=0;InitStatus=0;ReturnNull=FALSE;Creates=TRUE;Result=0;}
static void Run(DWORD expected){KxAdvapiTryInheritService(L"VendorSvc");assert(Calls==expected&&Logs==(DWORD)(expected&&Creates)&&Error==333);}
int main(void){
    Reset();Run(1);
    Reset();Data.Flags=KEXDATA_FLAG_PROPAGATED;Run(1);
    Reset();Data.Flags=0;Run(0);
    Reset();Data.IfeoParameters.DisableForChild=1;Run(0);
    Reset();InitStatus=(NTSTATUS)0xc0000001U;Run(0);
    Reset();ReturnNull=TRUE;Run(0);
    Reset();Creates=FALSE;Run(1);
    Reset();Creates=FALSE;Result=ERROR_ACCESS_DENIED;Run(1);
    puts("SCM-to-Core bridge: 8 actual-function scenarios passed; effective flags, exact profile pointer and native LastError preserved.");return 0;
}
