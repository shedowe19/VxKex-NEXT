#include "buildcfg.h"
#include <KexComm.h>
#include <KxCfgHlp.h>

VOID WINAPI KxAdvapiTryInheritService(PCWSTR ServiceName)
{
    PKEX_PROCESS_DATA Data;
    BOOLEAN Created;
    ULONG Error, SavedError;

    SavedError = GetLastError();
    Data = NULL;
    if (NT_SUCCESS(KexDataInitialize(&Data)) && Data &&
        (Data->Flags & (KEXDATA_FLAG_IFEO_OPTIONS_PRESENT | KEXDATA_FLAG_PROPAGATED)) &&
        !Data->IfeoParameters.DisableForChild) {
        Error = KxCfgTryInheritServiceParameters(ServiceName, &Data->IfeoParameters, &Created);
        if (Created) {
            VxlWriteLog(Data->LogHandle, KEX_COMPONENT, LogSeverityInformation,
                L"Inherited VxKex configuration for local service %s.", ServiceName);
        }
        (VOID) Error;
    }
    SetLastError(SavedError);
}
