// Win7 does not support the automatic-proxy access type. Use a native direct
// session only when both current-user and WinHTTP settings are direct-only.
// PAC, WPAD, configured proxies and unsupported flags retain the native failure.
#include "buildcfg.h"
#include "kxnetp.h"
#pragma comment(lib, "winhttp.lib")

// Keep the existing architecture-specific ordinals, including unnamed Winsock
// export routes that share this provider DLL.
#ifdef KEX_ARCH_X64
#pragma comment(linker, "/EXPORT:WinHttpOpen=Ext_WinHttpOpen,@747")
#else
#pragma comment(linker, "/EXPORT:WinHttpOpen=_Ext_WinHttpOpen@20,@735")
#endif

STATIC BOOL KxNetDirectProxyStringEmpty(PCWSTR Value)
{
    return Value == NULL || Value[0] == L'\0';
}

KXNETAPI HINTERNET WINAPI Ext_WinHttpOpen(
    IN PCWSTR UserAgent,
    IN ULONG AccessType,
    IN PCWSTR ProxyName,
    IN PCWSTR ProxyBypass,
    IN ULONG Flags)
{
    HINTERNET Session;
    ULONG OriginalError;
    BOOL IeSuccess;
    BOOL DefaultSuccess;
    BOOL AllowDirect;
    BOOL CleanupSuccess;
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG IeConfig = {0};
    WINHTTP_PROXY_INFO DefaultConfig = {0};

    Session = WinHttpOpen(UserAgent, AccessType, ProxyName, ProxyBypass, Flags);
    OriginalError = GetLastError();
    if (Session != NULL || OriginalError != ERROR_INVALID_PARAMETER ||
        AccessType != 4 || ProxyName != NULL || ProxyBypass != NULL ||
        (Flags != 0 && Flags != WINHTTP_FLAG_ASYNC)) {
        return Session;
    }

    IeSuccess = WinHttpGetIEProxyConfigForCurrentUser(&IeConfig);
    DefaultSuccess = FALSE;
    if (IeSuccess) {
        DefaultSuccess = WinHttpGetDefaultProxyConfiguration(&DefaultConfig);
    }
    AllowDirect = IeSuccess && DefaultSuccess &&
        !IeConfig.fAutoDetect &&
        KxNetDirectProxyStringEmpty(IeConfig.lpszAutoConfigUrl) &&
        KxNetDirectProxyStringEmpty(IeConfig.lpszProxy) &&
        KxNetDirectProxyStringEmpty(IeConfig.lpszProxyBypass) &&
        DefaultConfig.dwAccessType == WINHTTP_ACCESS_TYPE_NO_PROXY &&
        KxNetDirectProxyStringEmpty(DefaultConfig.lpszProxy) &&
        KxNetDirectProxyStringEmpty(DefaultConfig.lpszProxyBypass);

    CleanupSuccess = TRUE;
    if (IeConfig.lpszAutoConfigUrl != NULL && GlobalFree(IeConfig.lpszAutoConfigUrl) != NULL) CleanupSuccess = FALSE;
    if (IeConfig.lpszProxy != NULL && GlobalFree(IeConfig.lpszProxy) != NULL) CleanupSuccess = FALSE;
    if (IeConfig.lpszProxyBypass != NULL && GlobalFree(IeConfig.lpszProxyBypass) != NULL) CleanupSuccess = FALSE;
    if (DefaultConfig.lpszProxy != NULL && GlobalFree(DefaultConfig.lpszProxy) != NULL) CleanupSuccess = FALSE;
    if (DefaultConfig.lpszProxyBypass != NULL && GlobalFree(DefaultConfig.lpszProxyBypass) != NULL) CleanupSuccess = FALSE;

    // Auxiliary queries/frees must not replace the original unsupported result.
    SetLastError(OriginalError);
    if (!AllowDirect || !CleanupSuccess) {
        return NULL;
    }

    // A real native session under a verified direct-only configuration snapshot.
    // Keep agent, null proxy/bypass and all accepted flags exactly as supplied.
    return WinHttpOpen(UserAgent, WINHTTP_ACCESS_TYPE_NO_PROXY,
                       ProxyName, ProxyBypass, Flags);
}
