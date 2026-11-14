// A parent-process attribute also selects the token inherited by the child.
// Elevated/High loaders always retain their own exact token and job context.
// Retain the Explorer parent only for matching non-elevated security contexts.
// Missing query rights or incomplete token data use ordinary CreateProcess.
#pragma once

STATIC BOOLEAN VklCanUseParentProcess(IN HANDLE ParentProcess)
{
    HANDLE Tokens[2];
    TOKEN_ELEVATION Elevation[2];
    ULONG Session[2], Integrity[2], Index, Required, SavedError;
    UCHAR Count;
    BOOLEAN Compatible;
    union {
        TOKEN_USER User;
        BYTE Bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    } Users[2];
    union {
        TOKEN_MANDATORY_LABEL Label;
        BYTE Bytes[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    } Labels[2];

    SavedError = GetLastError();
    Tokens[0] = Tokens[1] = NULL;
    Compatible = FALSE;
    for (Index = 0; Index < 2; ++Index) {
        if (!OpenProcessToken(Index ? ParentProcess : GetCurrentProcess(),
            TOKEN_QUERY, &Tokens[Index])) goto Exit;
        Required = 0;
        if (!GetTokenInformation(Tokens[Index], TokenUser, &Users[Index],
            sizeof(Users[Index]), &Required) || Required < sizeof(TOKEN_USER) ||
            Required > sizeof(Users[Index])) goto Exit;
        if (!GetTokenInformation(Tokens[Index], TokenElevation, &Elevation[Index],
            sizeof(Elevation[Index]), &Required) || Required != sizeof(Elevation[Index])) goto Exit;
        if (!GetTokenInformation(Tokens[Index], TokenSessionId, &Session[Index],
            sizeof(Session[Index]), &Required) || Required != sizeof(Session[Index])) goto Exit;
        if (!GetTokenInformation(Tokens[Index], TokenIntegrityLevel, &Labels[Index],
            sizeof(Labels[Index]), &Required) || Required < sizeof(TOKEN_MANDATORY_LABEL) ||
            Required > sizeof(Labels[Index])) goto Exit;
        if (!IsValidSid(Users[Index].User.User.Sid) ||
            !IsValidSid(Labels[Index].Label.Label.Sid)) goto Exit;
        Count = *GetSidSubAuthorityCount(Labels[Index].Label.Label.Sid);
        if (!Count) goto Exit;
        Integrity[Index] = *GetSidSubAuthority(Labels[Index].Label.Label.Sid, Count - 1);
        if (!Index && (Elevation[Index].TokenIsElevated ||
            Integrity[Index] >= SECURITY_MANDATORY_HIGH_RID)) goto Exit;
    }
    Compatible = EqualSid(Users[0].User.User.Sid, Users[1].User.User.Sid) &&
        Session[0] == Session[1] && Integrity[0] == Integrity[1] &&
        Elevation[0].TokenIsElevated == Elevation[1].TokenIsElevated;
Exit:
    if (Tokens[1]) CloseHandle(Tokens[1]);
    if (Tokens[0]) CloseHandle(Tokens[0]);
    SetLastError(SavedError);
    return Compatible;
}
