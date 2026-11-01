#include "service_test_shim.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#define K(n) ((HKEY)(ULONG_PTR)(n))
static TEST_KEX_PROCESS_DATA Data;
TEST_KEX_PROCESS_DATA *KexData=&Data;
static DWORD Error,ExpectedView,ServiceCalls,QueryCalls,OpenKeys,GuidCalls,Logs,Scenarios;
static int UserClass,UserApp,OpenFault,TreatAs,AutoTreatAs,Remote,GuidFault,FormatFault,AppMalformed,ServiceMalformed,HelperCreates;
static ULONG HelperResult;
static const WCHAR ClassText[]=L"{11111111-2222-3333-4444-555555555555}";
static const WCHAR AppText[]=L"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}";
static const CLSID ClassId={0x11111111,0x2222,0x3333,{0x44,0x44,0x55,0x55,0x55,0x55,0x55,0x55}};
DWORD WINAPI MockGetLastError(VOID){return Error;}
VOID WINAPI MockSetLastError(DWORD value){Error=value;}
static void Copy(PWSTR out,PCWSTR in){memcpy(out,in,(wcslen(in)+1)*sizeof(WCHAR));}
INT WINAPI MockStringFromGUID2(REFCLSID clsid,PWSTR out,INT capacity){++GuidCalls;assert(capacity==40);Error=901;if(GuidFault==(int)GuidCalls)return 0;Copy(out,clsid->Data1==ClassId.Data1?ClassText:AppText);return 39;}
HRESULT WINAPI MockCLSIDFromString(PWSTR text,CLSID *result){assert(!wcscmp(text,AppText));Error=902;if(GuidFault==3)return E_FAIL;memset(result,0,sizeof(*result));result->Data1=0xaaaaaaaaU;return S_OK;}
HRESULT MockStringCchPrintf(PWSTR out,size_t capacity,PCWSTR format,...){va_list args;PCWSTR text;assert(capacity==128);Error=903;if(FormatFault)return E_FAIL;va_start(args,format);text=va_arg(args,PCWSTR);va_end(args);Copy(out,wcsstr(format,L"CLSID")?L"Software\\Classes\\CLSID\\":L"Software\\Classes\\AppID\\");Copy(out+wcslen(out),text);return S_OK;}
LONG WINAPI MockRegOpenKeyExW(HKEY parent,PCWSTR path,DWORD reserved,DWORD rights,HKEY *result){
    int isClass;assert(!reserved);Error=904;
    if(parent==HKEY_CURRENT_USER){assert(rights==(KEY_READ|ExpectedView));isClass=wcsstr(path,L"CLSID")!=NULL;if((isClass&&UserClass)||(!isClass&&UserApp)){*result=K(203);++OpenKeys;return 0;}return ERROR_FILE_NOT_FOUND;}
    if(parent==HKEY_LOCAL_MACHINE){assert(rights==(KEY_QUERY_VALUE|ExpectedView));isClass=wcsstr(path,L"CLSID")!=NULL;if(OpenFault==(isClass?1:2))return ERROR_ACCESS_DENIED;*result=K(isClass?201:202);++OpenKeys;return 0;}
    assert(parent==K(201)&&rights==(KEY_READ|ExpectedView));if((TreatAs&&!wcscmp(path,L"TreatAs"))||(AutoTreatAs&&!wcscmp(path,L"AutoTreatAs"))){*result=K(203);++OpenKeys;return 0;}return ERROR_FILE_NOT_FOUND;
}
LONG WINAPI MockRegQueryValueExW(HKEY key,PCWSTR name,LPDWORD reserved,LPDWORD type,PBYTE data,LPDWORD bytes){
    const WCHAR *text;size_t size;assert(!reserved);++QueryCalls;Error=905;
    if(key==K(202)&&!wcscmp(name,L"RemoteServerName")){assert(!type&&!data&&!bytes);return Remote==2?ERROR_ACCESS_DENIED:Remote?ERROR_SUCCESS:ERROR_FILE_NOT_FOUND;}
    assert(type&&data&&bytes);text=key==K(201)?AppText:L"VendorSvc";assert(!wcscmp(name,key==K(201)?L"AppID":L"LocalService"));size=(wcslen(text)+1)*sizeof(WCHAR);assert(*bytes>=size+sizeof(WCHAR));memcpy(data,text,size);*type=REG_SZ;*bytes=(DWORD)size;
    if(key==K(201)){if(AppMalformed==1)*type=REG_EXPAND_SZ;if(AppMalformed==2)*bytes-=sizeof(WCHAR);if(AppMalformed==3)((PWSTR)data)[38]=L'X';}
    else{if(ServiceMalformed==1)*type=REG_EXPAND_SZ;if(ServiceMalformed==2)((PWSTR)data)[size/sizeof(WCHAR)-1]=L'X';if(ServiceMalformed==3){((PWSTR)data)[size/sizeof(WCHAR)]=0;*bytes+=sizeof(WCHAR);}if(ServiceMalformed==4){((PWSTR)data)[0]=0;*bytes=sizeof(WCHAR);}}
    return 0;
}
LONG WINAPI MockRegCloseKey(HKEY key){assert((key==K(201)||key==K(202)||key==K(203))&&OpenKeys);--OpenKeys;Error=906;return 0;}
ULONG KXCFGAPI KxCfgTryInheritServiceParameters(PCWSTR name,const KEX_IFEO_PARAMETERS *parameters,PBOOLEAN created){assert(!wcscmp(name,L"VendorSvc")&&parameters==&Data.IfeoParameters);++ServiceCalls;*created=(BOOLEAN)HelperCreates;Error=907;return HelperResult;}
VOID MockInformationEvent(PCWSTR format,...){assert(format&&HelperCreates);++Logs;Error=999;}
static void Reset(void){assert(!OpenKeys);memset(&Data,0,sizeof(Data));Data.Flags=KEXDATA_FLAG_IFEO_OPTIONS_PRESENT;Data.IfeoParameters.WinVerSpoof=WinVerSpoofWin10;Data.IfeoParameters.DisableAppSpecific=1;Data.IfeoParameters.StrongVersionSpoof=2;Data.IfeoParameters.DisableConsoleEnhancements=1;Data.IfeoParameters.TlsForceEnabledProtocols=0x800;Data.IfeoParameters.TlsForceDisabledProtocols=0x300;KexData=&Data;Error=333;ExpectedView=ServiceCalls=QueryCalls=GuidCalls=Logs=0;UserClass=UserApp=OpenFault=TreatAs=AutoTreatAs=Remote=GuidFault=FormatFault=AppMalformed=ServiceMalformed=0;HelperCreates=1;HelperResult=0;}
static void Run(ULONG context,DWORD expected){KxCompTryInheritServiceForClass(&ClassId,context);assert(Error==333&&!OpenKeys&&ServiceCalls==expected&&Logs==(DWORD)(expected&&HelperCreates));++Scenarios;}
int main(void){unsigned i;
    Reset();Run(CLSCTX_LOCAL_SERVER,1);
    Reset();Data.Flags=KEXDATA_FLAG_PROPAGATED;Run(CLSCTX_LOCAL_SERVER,1);
    Reset();ExpectedView=KEY_WOW64_32KEY;Run(CLSCTX_LOCAL_SERVER|CLSCTX_ACTIVATE_32_BIT_SERVER,1);
    Reset();ExpectedView=KEY_WOW64_64KEY;Run(CLSCTX_LOCAL_SERVER|CLSCTX_ACTIVATE_64_BIT_SERVER,1);
    Reset();Run(CLSCTX_LOCAL_SERVER|CLSCTX_ACTIVATE_32_BIT_SERVER|CLSCTX_ACTIVATE_64_BIT_SERVER,0);
    Reset();Run(CLSCTX_REMOTE_SERVER,0);
    Reset();KexData=NULL;Run(CLSCTX_LOCAL_SERVER,0);
    Reset();Data.Flags=0;Run(CLSCTX_LOCAL_SERVER,0);
    Reset();Data.IfeoParameters.DisableForChild=1;Run(CLSCTX_LOCAL_SERVER,0);
    Reset();UserClass=1;Run(CLSCTX_LOCAL_SERVER,0);
    Reset();UserApp=1;Run(CLSCTX_LOCAL_SERVER,0);
    Reset();TreatAs=1;Run(CLSCTX_LOCAL_SERVER,0);
    Reset();AutoTreatAs=1;Run(CLSCTX_LOCAL_SERVER,0);
    for(i=1;i<=2;++i){Reset();Remote=(int)i;Run(CLSCTX_LOCAL_SERVER,0);}
    for(i=1;i<=2;++i){Reset();OpenFault=(int)i;Run(CLSCTX_LOCAL_SERVER,0);}
    for(i=1;i<=3;++i){Reset();GuidFault=(int)i;Run(CLSCTX_LOCAL_SERVER,0);}
    Reset();FormatFault=1;Run(CLSCTX_LOCAL_SERVER,0);
    for(i=1;i<=3;++i){Reset();AppMalformed=(int)i;Run(CLSCTX_LOCAL_SERVER,0);}
    for(i=1;i<=4;++i){Reset();ServiceMalformed=(int)i;Run(CLSCTX_LOCAL_SERVER,0);}
    Reset();HelperResult=ERROR_ACCESS_DENIED;HelperCreates=0;Run(CLSCTX_LOCAL_SERVER,1);
    printf("COM LocalService dispatch: %lu actual-function scenarios passed; no registry or COM activation.\n",(unsigned long)Scenarios);return 0;
}
