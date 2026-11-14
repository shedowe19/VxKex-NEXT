// Commit an exact-image service profile only when no target profile exists.
// Keep this independent of KxCfgSetConfiguration: no elevation, cleanup or UI.
#ifdef KEX_SERVICE_TEST
#include "service_test_shim.h"
#else
#include "buildcfg.h"
#include <KexComm.h>
#include <KxCfgHlp.h>
#endif

KXCFGDECLSPEC ULONG KXCFGAPI KxCfgTryInheritConfiguration(
    PCWSTR ExeFullPath, CONST KEX_IFEO_PARAMETERS *Parameters, PBOOLEAN Created)
{
    static CONST PCWSTR ParameterNames[] = {
        L"KEX_DisableForChild", L"KEX_DisableAppSpecific", L"KEX_WinVerSpoof",
        L"KEX_StrongVersionSpoof", L"KEX_DisableConsoleEnhancements",
        L"KEX_TlsForceEnabledProtocols", L"KEX_TlsForceDisabledProtocols"
    };
    HANDLE Transaction;
    HKEY Image, Child, Existing;
    ULONG Error, Index, Length, Type, UseFilter, Disposition, Hash, Value;
    ULONG ParameterValues[7];
    WCHAR KeyPath[MAX_PATH], Name[MAX_PATH], Path[MAX_PATH];
    PCWSTR BaseName, Cursor;
    NTSTATUS Status;
    BOOLEAN Committed, FilterActive;

    if (!Created) return ERROR_INVALID_PARAMETER;
    *Created = FALSE;
    if (!ExeFullPath || !Parameters || Parameters->DisableForChild ||
        Parameters->DisableAppSpecific > 1 ||
        Parameters->DisableConsoleEnhancements > 1 ||
        (ULONG) Parameters->WinVerSpoof >= WinVerSpoofMax ||
        (Parameters->StrongVersionSpoof & ~KEX_STRONGSPOOF_VALID_MASK)) {
        return ERROR_INVALID_PARAMETER;
    }
    Length = (ULONG) wcslen(ExeFullPath);
    if (Length < 7 || Length >= MAX_PATH || ExeFullPath[1] != L':' ||
        ExeFullPath[2] != L'\\' ||
        !((ExeFullPath[0] >= L'A' && ExeFullPath[0] <= L'Z') ||
          (ExeFullPath[0] >= L'a' && ExeFullPath[0] <= L'z'))) {
        return ERROR_BAD_PATHNAME;
    }
    BaseName = wcsrchr(ExeFullPath, L'\\');
    if (!BaseName || !BaseName[1]) return ERROR_BAD_PATHNAME;
    ++BaseName;
    if (!StringEqualI(ExeFullPath + Length - 4, L".exe")) return ERROR_BAD_PATHNAME;

    Transaction = CreateSimpleTransaction(L"VxKex service propagation");
    if (!Transaction) return GetLastError();
    Image = Child = Existing = NULL;
    Committed = FALSE;
    FilterActive = FALSE;
    Error = ERROR_SUCCESS;
    if (FAILED(StringCchPrintf(KeyPath, ARRAYSIZE(KeyPath),
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\%s", BaseName))) {
        Error = ERROR_BAD_PATHNAME;
        goto Exit;
    }
    Error = RegCreateKeyTransactedW(HKEY_LOCAL_MACHINE, KeyPath, 0, NULL, 0,
        KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, NULL, &Image, NULL,
        Transaction, NULL);
    if (Error) goto Exit;

    // Any basename-level policy except these two neutral values is explicit.
    // Do not turn legacy/global policies into a different filtered policy.
    for (Index = 0; ; ++Index) {
        Length = ARRAYSIZE(Name);
        Error = RegEnumValueW(Image, Index, Name, &Length, NULL, NULL, NULL, NULL);
        if (Error == ERROR_NO_MORE_ITEMS) break;
        if (Error) goto Exit;
        if (!StringEqualI(Name, L"UseFilter") &&
            !StringEqualI(Name, L"DisableExceptionChainValidation")) {
            Error = ERROR_ALREADY_EXISTS;
            goto Exit;
        }
    }
    Length = sizeof(UseFilter);
    Error = RegQueryValueExW(Image, L"UseFilter", NULL, &Type,
        (PBYTE) &UseFilter, &Length);
    if (Error == ERROR_SUCCESS) {
        if (Type != REG_DWORD || Length != sizeof(UseFilter) || UseFilter != 1) {
            Error = ERROR_ALREADY_EXISTS;
            goto Exit;
        }
        FilterActive = TRUE;
    } else if (Error != ERROR_FILE_NOT_FOUND) goto Exit;

    // Check every existing exact-path filter within the same transaction.
    // An explicit disabled or differently configured target always wins.
    for (Index = 0; ; ++Index) {
        Length = ARRAYSIZE(Name);
        Error = RegEnumKeyExW(Image, Index, Name, &Length, NULL, NULL, NULL, NULL);
        if (Error == ERROR_NO_MORE_ITEMS) break;
        if (Error) goto Exit;
        if (!FilterActive) {
            // Enabling filtering must not activate another dormant profile.
            Error = ERROR_ALREADY_EXISTS;
            goto Exit;
        }
        Error = RegOpenKeyTransactedW(Image, Name, 0, KEY_READ,
            &Existing, Transaction, NULL);
        if (Error) goto Exit;
        Length = sizeof(Path);
        Error = RegQueryValueExW(Existing, L"FilterFullPath", NULL, &Type,
            (PBYTE) Path, &Length);
        RegCloseKey(Existing);
        Existing = NULL;
        if (Error || Type != REG_SZ || Length < sizeof(WCHAR) ||
            Length > sizeof(Path) || Length % sizeof(WCHAR) ||
            Path[Length / sizeof(WCHAR) - 1] != L'\0' ||
            (wcslen(Path) + 1) * sizeof(WCHAR) != Length) {
            if (!Error) Error = ERROR_INVALID_DATA;
            goto Exit;
        }
        if (StringEqualI(Path, ExeFullPath)) {
            Error = ERROR_ALREADY_EXISTS;
            goto Exit;
        }
    }

    Hash = 0;
    for (Cursor = ExeFullPath; *Cursor; ++Cursor) {
        WCHAR C;
        C = *Cursor;
        if (C >= L'a' && C <= L'z') C -= L'a' - L'A';
        Hash = Hash * 65599 + C;
    }
    StringCchPrintf(Name, ARRAYSIZE(Name), L"VxKex_Service_%08lx", Hash);
    Error = RegCreateKeyTransactedW(Image, Name, 0, NULL, 0,
        KEY_READ | KEY_WRITE, NULL, &Child, &Disposition, Transaction, NULL);
    if (Error) goto Exit;
    if (Disposition != REG_CREATED_NEW_KEY) {
        Error = ERROR_ALREADY_EXISTS;
        goto Exit;
    }
    Length = ((ULONG) wcslen(ExeFullPath) + 1) * sizeof(WCHAR);
    Error = RegSetValueExW(Child, L"FilterFullPath", 0, REG_SZ,
        (CONST BYTE *) ExeFullPath, Length);
    if (Error) goto Exit;
    Value = FLG_APPLICATION_VERIFIER;
    Error = RegSetValueExW(Child, L"GlobalFlag", 0, REG_DWORD,
        (CONST BYTE *) &Value, sizeof(Value));
    if (Error) goto Exit;
    Value = 0x80000000;
    Error = RegSetValueExW(Child, L"VerifierFlags", 0, REG_DWORD,
        (CONST BYTE *) &Value, sizeof(Value));
    if (Error) goto Exit;
    Error = RegSetValueExW(Child, L"VerifierDlls", 0, REG_SZ,
        (CONST BYTE *) L"KexDll.dll", sizeof(L"KexDll.dll"));
    if (Error) goto Exit;
    ParameterValues[0] = Parameters->DisableForChild;
    ParameterValues[1] = Parameters->DisableAppSpecific;
    ParameterValues[2] = Parameters->WinVerSpoof;
    ParameterValues[3] = Parameters->StrongVersionSpoof;
    ParameterValues[4] = Parameters->DisableConsoleEnhancements;
    ParameterValues[5] = Parameters->TlsForceEnabledProtocols;
    ParameterValues[6] = Parameters->TlsForceDisabledProtocols;
    for (Index = 0; Index < ARRAYSIZE(ParameterNames); ++Index) {
        Error = RegSetValueExW(Child, ParameterNames[Index], 0, REG_DWORD,
            (CONST BYTE *) &ParameterValues[Index], sizeof(ULONG));
        if (Error) goto Exit;
    }
    UseFilter = 1;
    Error = RegSetValueExW(Image, L"UseFilter", 0, REG_DWORD,
        (CONST BYTE *) &UseFilter, sizeof(UseFilter));
    if (Error) goto Exit;
    Status = NtCommitTransaction(Transaction, TRUE);
    if (!NT_SUCCESS(Status)) {
        Error = RtlNtStatusToDosError(Status);
        goto Exit;
    }
    Committed = TRUE;
    *Created = TRUE;
    Error = ERROR_SUCCESS;
Exit:
    if (Existing) RegCloseKey(Existing);
    if (Child) RegCloseKey(Child);
    if (Image) RegCloseKey(Image);
    if (!Committed) NtRollbackTransaction(Transaction, TRUE);
    NtClose(Transaction);
    return Error;
}
