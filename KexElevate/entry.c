#include "buildcfg.h"
#include <KexUac.h>

DWORD WINAPI KxuElevateMain(VOID);

VOID WINAPI EntryPoint(VOID)
{
    ExitProcess(KxuElevateMain());
}

