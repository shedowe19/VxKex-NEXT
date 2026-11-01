#include "uac_test_shim.h"
/* ACTUAL_PROTOCOL */
/* ACTUAL_FUNCTIONS */
static KXU_REQUEST Request;
static void Reset(void)
{
    ZeroMemory(&Request,sizeof(Request));
    KxuHeader(&Request.Header,KxuRequest,sizeof(Request));
    Request.CallerPid=123; Request.CallerCreationTime=456;
    memcpy(Request.File,L"C:\\Vendor\\app.exe",sizeof(L"C:\\Vendor\\app.exe"));
    memcpy(Request.Directory,L"C:\\Vendor",sizeof(L"C:\\Vendor"));
    Request.FileCch=(ULONG)wcslen(Request.File)+1;
    Request.DirectoryCch=(ULONG)wcslen(Request.Directory)+1;
    Request.ArgumentsCch=1;
    Request.Parameters.DisableAppSpecific=1;
    Request.Parameters.WinVerSpoof=WinVerSpoofWin10;
    Request.Parameters.StrongVersionSpoof=3;
    Request.Parameters.TlsForceEnabledProtocols=0x800;
    Request.Parameters.TlsForceDisabledProtocols=0x80;
    Request.Parameters.DisableConsoleEnhancements=1;
}
int main(void)
{
    KEX_IFEO_PARAMETERS Saved;
    WCHAR Text[64]; ULONG Pid,Index; ULONGLONG Time; FILETIME FileTime;
#ifdef _WIN32
    CHECK(sizeof(WCHAR)==2); CHECK(sizeof(KXU_REQUEST)==66680);
#endif
    CHECK(sizeof(KXU_HEADER)==16); CHECK(sizeof(KXU_MESSAGE)==32);
    CHECK(KXU_TARGET_ACCESS==0x101000);
    Reset(); Saved=Request.Parameters;
    CHECK(KxuValidateRequest(&Request,123,456)==0);
    CHECK(!memcmp(&Saved,&Request.Parameters,sizeof(Saved)));
    CHECK(KxuValidateRequest(&Request,124,456)==ERROR_INVALID_PARAMETER);
    CHECK(KxuValidateRequest(&Request,123,457)==ERROR_INVALID_PARAMETER);
    Request.Header.Magic^=1; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Header.Version++; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Header.Type=KxuStarted; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Header.Size--; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Reserved=1; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Parameters.DisableForChild=1; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Parameters.DisableAppSpecific=2; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Parameters.DisableConsoleEnhancements=2; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Parameters.WinVerSpoof=(KEX_WIN_VER_SPOOF)-1; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Parameters.WinVerSpoof=WinVerSpoofMax; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Parameters.StrongVersionSpoof=4; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.Parameters.TlsForceDisabledProtocols=0x800; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.ShowWindow=-1; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.ShowWindow=12; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    for(Index=0;Index<=11;++Index) { Request.ShowWindow=(LONG)Index; CHECK(KxuValidateRequest(&Request,123,456)==0); }
    Reset(); Request.FileCch=0; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.DirectoryCch=MAX_PATH+1; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.File[4]=0; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.File[Request.FileCch-1]=L'a'; CHECK(KxuValidateRequest(&Request,123,456)==87); Reset();
    Request.ArgumentsCch=KXU_ARGUMENT_CCH;
    for(Index=0;Index<KXU_ARGUMENT_CCH-1;++Index) Request.Arguments[Index]=L'a';
    CHECK(KxuValidateRequest(&Request,123,456)==ERROR_FILENAME_EXCED_RANGE);
    CHECK(KxuLocalPath(L"C:\\Program Files\\App.EXE",TRUE));
    CHECK(!KxuLocalPath(L"relative.exe",TRUE)); CHECK(!KxuLocalPath(L"\\\\server\\share\\app.exe",TRUE));
    CHECK(!KxuLocalPath(L"C:\\app.exe:stream",TRUE)); CHECK(!KxuLocalPath(L"C:\\app.exe%",TRUE));
    CHECK(!KxuLocalPath(L"C:\\a\n.exe",TRUE)); CHECK(!KxuLocalPath(L"C:\\app.dll",TRUE));
    CHECK(!KxuLocalPath(L"C:/app.exe",TRUE)); CHECK(!KxuLocalPath(L"C:\\a?.exe",TRUE));
    CHECK(KxuLocalPath(L"C:\\",FALSE)); CHECK(!KxuValidParameters(NULL));
    FileTime.dwHighDateTime=0x12345678; FileTime.dwLowDateTime=0x9abcdef0;
    CHECK(KxuFileTime(FileTime)==(((ULONGLONG)0x12345678<<32)|0x9abcdef0));
    memcpy(Text,L"0123456789abcdef0123456789abcdef",sizeof(L"0123456789abcdef0123456789abcdef"));
    CHECK(KxuNonceValid(Text)); Text[0]=L'A'; CHECK(!KxuNonceValid(Text));
    Text[0]=L'0'; Text[31]=0; CHECK(!KxuNonceValid(Text));
    CHECK(KxuParsePid(L"4294967295",&Pid)&&Pid==MAXDWORD);
    CHECK(!KxuParsePid(L"4294967296",&Pid)); CHECK(!KxuParsePid(L"0",&Pid));
    CHECK(!KxuParsePid(L"-1",&Pid)); CHECK(!KxuParsePid(L"1x",&Pid)); CHECK(!KxuParsePid(L"",&Pid));
    CHECK(KxuParseCreationTime(L"0123456789ABCDEF",&Time)&&Time==0x0123456789abcdefULL);
    CHECK(!KxuParseCreationTime(L"0000000000000000",&Time));
    CHECK(!KxuParseCreationTime(L"0123456789abcdeg",&Time));
    CHECK(!KxuParseCreationTime(L"0123456789abcdef0",&Time));
    printf("UAC protocol/admission: %d checks passed.\n",Checks); return 0;
}
