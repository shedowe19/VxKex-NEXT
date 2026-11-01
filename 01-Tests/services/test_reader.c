/* Exercise the actual seven registry-query statements from Core, not its
   unrelated MSI/PEB selection logic. No NTDLL or registry call is executed. */
#include "service_test_shim.h"
#include <assert.h>
#include <stdio.h>
static KEX_IFEO_PARAMETERS *Current;
static unsigned Calls;
static int Missing;
static const ULONG Values[7]={0,1,4,2,1,0x800,0x300};
static const WCHAR *Names[7]={L"KEX_DisableForChild",L"KEX_DisableAppSpecific",L"KEX_WinVerSpoof",L"KEX_StrongVersionSpoof",L"KEX_DisableConsoleEnhancements",L"KEX_TlsForceEnabledProtocols",L"KEX_TlsForceDisabledProtocols"};
NTSTATUS WINAPI MockLdrQueryImageFileKeyOption(HANDLE key,PCWSTR name,ULONG type,PVOID output,ULONG bytes,ULONG *returned){
    unsigned index=Calls++;assert(index<7&&key==(HANDLE)(ULONG_PTR)7&&type==REG_DWORD&&bytes==4&&!returned);assert(!wcscmp(name,Names[index]));assert((BYTE*)output==(BYTE*)Current+4*index);
    if(Missing==-2||Missing==(int)index)return(NTSTATUS)0xc0000034U;
    memcpy(output,&Values[index],4);return 0;
}
static NTSTATUS ReadActualQueryBlock(KEX_IFEO_PARAMETERS *IfeoParameters){
    HANDLE IfeoKeyHandle=(HANDLE)(ULONG_PTR)7;
    NTSTATUS NoKexOptionsInRegistry=(NTSTATUS)-1;
    /* PRODUCTION_READER_BLOCK */
    return NoKexOptionsInRegistry;
}
int main(void){KEX_IFEO_PARAMETERS data;NTSTATUS status;int missing;unsigned i;ULONG value;
    assert(sizeof(data)==28);
    for(missing=-2;missing<7;++missing){memset(&data,0xa5,sizeof(data));Current=&data;Calls=0;Missing=missing;status=ReadActualQueryBlock(&data);assert(Calls==7);assert(NT_SUCCESS(status)==(missing!=-2));for(i=0;i<7;++i){memcpy(&value,(BYTE*)&data+4*i,4);assert(value==((missing==-2||missing==(int)i)?0xa5a5a5a5U:Values[i]));}}
    puts("Core IFEO reader: 9 actual-readblock scenarios passed; all seven DWORD fields and missing values checked.");return 0;
}
