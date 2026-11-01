#include "buildcfg.h"
#include <KexUac.h>

PKEX_PROCESS_DATA KexData;

BOOL WINAPI DllMain(PVOID DllBase, ULONG Reason, PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    if (Reason == DLL_PROCESS_ATTACH) {
        LdrDisableThreadCalloutsForDll(DllBase);
        if (!NT_SUCCESS(KexDataInitialize(&KexData))) return FALSE;
    }
    return TRUE;
}

