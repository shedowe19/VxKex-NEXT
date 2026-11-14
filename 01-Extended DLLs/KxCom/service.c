// COM LocalService activation is brokered by SCM rather than a child creation.
// Prepare only an eligible local service image, then leave activation to COM.
#ifdef KEX_SERVICE_TEST
#include "service_test_shim.h"
#else
#include "buildcfg.h"
#include "kxcomp.h"
#include <KxCfgHlp.h>
#endif

VOID WINAPI KxCompTryInheritServiceForClass(REFCLSID ClassId, ULONG ClassContext)
{
    WCHAR Guid[40], AppId[40], Path[128], Service[257];
    CLSID ParsedAppId;
    HKEY Key, Other;
    ULONG Error, SavedError, Type, Bytes, View;
    BOOLEAN Created;

    SavedError = GetLastError();
    Key = NULL;
    Other = NULL;
    if (!KexData || !(ClassContext & CLSCTX_LOCAL_SERVER) ||
        !(KexData->Flags & (KEXDATA_FLAG_IFEO_OPTIONS_PRESENT | KEXDATA_FLAG_PROPAGATED)) ||
        KexData->IfeoParameters.DisableForChild) goto Exit;
    if ((ClassContext & CLSCTX_ACTIVATE_32_BIT_SERVER) &&
        (ClassContext & CLSCTX_ACTIVATE_64_BIT_SERVER)) goto Exit;
    View = (ClassContext & CLSCTX_ACTIVATE_32_BIT_SERVER) ? KEY_WOW64_32KEY :
        ((ClassContext & CLSCTX_ACTIVATE_64_BIT_SERVER) ? KEY_WOW64_64KEY : 0);
    if (!StringFromGUID2(ClassId, Guid, ARRAYSIZE(Guid))) goto Exit;
    if (FAILED(StringCchPrintf(Path, ARRAYSIZE(Path), L"Software\\Classes\\CLSID\\%s", Guid))) goto Exit;
    // A user override or class redirection can select a different registration.
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_READ | View, &Other);
    if (Error != ERROR_FILE_NOT_FOUND) goto Exit;
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_QUERY_VALUE | View, &Key);
    if (Error) goto Exit;
    Error = RegOpenKeyExW(Key, L"TreatAs", 0, KEY_READ | View, &Other);
    if (Error != ERROR_FILE_NOT_FOUND) goto Exit;
    Error = RegOpenKeyExW(Key, L"AutoTreatAs", 0, KEY_READ | View, &Other);
    if (Error != ERROR_FILE_NOT_FOUND) goto Exit;
    Bytes = sizeof(AppId);
    Error = RegQueryValueExW(Key, L"AppID", NULL, &Type, (PBYTE) AppId, &Bytes);
    RegCloseKey(Key);
    Key = NULL;
    if (Error || Type != REG_SZ || Bytes != 39 * sizeof(WCHAR) ||
        AppId[0] != L'{' || AppId[37] != L'}' || AppId[38] ||
        FAILED(CLSIDFromString(AppId, &ParsedAppId))) goto Exit;
    if (!StringFromGUID2(&ParsedAppId, Guid, ARRAYSIZE(Guid))) goto Exit;
    if (FAILED(StringCchPrintf(Path, ARRAYSIZE(Path), L"Software\\Classes\\AppID\\%s", Guid))) goto Exit;
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_READ | View, &Other);
    if (Error != ERROR_FILE_NOT_FOUND) goto Exit;
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_QUERY_VALUE | View, &Key);
    if (Error) goto Exit;
    // An explicit remote registration must not configure a local image.
    Error = RegQueryValueExW(Key, L"RemoteServerName", NULL, NULL, NULL, NULL);
    if (Error != ERROR_FILE_NOT_FOUND) goto Exit;
    Bytes = sizeof(Service);
    Error = RegQueryValueExW(Key, L"LocalService", NULL, &Type, (PBYTE) Service, &Bytes);
    if (Error || Type != REG_SZ || Bytes < 2 * sizeof(WCHAR) ||
        Bytes > sizeof(Service) || Bytes % sizeof(WCHAR) ||
        Service[Bytes / sizeof(WCHAR) - 1] ||
        (wcslen(Service) + 1) * sizeof(WCHAR) != Bytes) goto Exit;
    Error = KxCfgTryInheritServiceParameters(Service, &KexData->IfeoParameters, &Created);
    if (Created) {
        KexLogInformationEvent(L"Inherited VxKex configuration for COM LocalService %s.", Service);
    }
    (VOID) Error;
Exit:
    if (Other) RegCloseKey(Other);
    if (Key) RegCloseKey(Key);
    SetLastError(SavedError);
}
