/* Execute the real CLI functions with isolated, deterministic Win32 doubles. */
#include <assert.h>
#include <ctype.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN
#define OUT
#define OPTIONAL
#define STATIC static
#define VOID void
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define MB_OK 0
#define MB_ICONERROR 1
#define MB_ICONINFORMATION 2
#define FRIENDLYAPPNAME_ENG L"VxKex NEXT"
#define _(s) (s)
#define ASSERT assert
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define StringLiteralLength(s) (ARRAYSIZE(s) - 1)
#define FAILED(s) ((s) < 0)
#define NT_SUCCESS(s) ((s) >= 0)
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_NOT_SUPPORTED (-2)
#define STATUS_UNSUCCESSFUL (-3)
#define ZeroMemory(p, n) memset((p), 0, (n))
#define until(c) while (!(c))

typedef unsigned char BOOLEAN;
typedef unsigned int ULONG;
typedef int INT;
typedef int NTSTATUS;
typedef int HRESULT;
typedef size_t SIZE_T;
typedef wchar_t WCHAR;
typedef WCHAR *PWSTR;
typedef WCHAR *PWCHAR;
typedef const WCHAR *PCWSTR;
typedef void *HANDLE;
typedef enum {
	WinVerSpoofNone, WinVerSpoofWin7, WinVerSpoofWin8,
	WinVerSpoofWin8Point1, WinVerSpoofWin10, WinVerSpoofWin11,
	WinVerSpoofMax
} KEX_WIN_VER_SPOOF;
#define KEX_STRONGSPOOF_VALID_MASK 3U

typedef struct {
	BOOLEAN Enabled, DisableForChild, DisableAppSpecificHacks;
	KEX_WIN_VER_SPOOF WinVerSpoof;
	ULONG StrongSpoofOptions;
} KXCFG_PROGRAM_CONFIGURATION;

static jmp_buf ExitJump;
static NTSTATUS ExitStatus;
static int Tests, Failures, Applied, Commits, Rollbacks, Closes;
static int FailCopy, FailCanonicalize;
static int FailWindowsDirectory, FailKexDirectory;
static BOOLEAN ExistingConfiguration;
static KXCFG_PROGRAM_CONFIGURATION Existing, Written;
static WCHAR WrittenPath[MAX_PATH], LastMessage[1024];

static SIZE_T WideLength(PCWSTR s)
{
	SIZE_T n = 0;
	while (s[n]) ++n;
	return n;
}

static WCHAR Upper(WCHAR c)
{
	return c >= L'a' && c <= L'z' ? c - L'a' + L'A' : c;
}

static int WideEqual(PCWSTR a, PCWSTR b)
{
	while (*a && *a == *b) { ++a; ++b; }
	return *a == *b;
}

static BOOLEAN StringEqualI(PCWSTR a, PCWSTR b)
{
	while (*a && Upper(*a) == Upper(*b)) { ++a; ++b; }
	return Upper(*a) == Upper(*b);
}

static BOOLEAN StringBeginsWithI(PCWSTR text, PCWSTR prefix)
{
	while (*prefix) if (Upper(*text++) != Upper(*prefix++)) return FALSE;
	return TRUE;
}

static PCWSTR FindText(PCWSTR text, PCWSTR needle, BOOLEAN insensitive)
{
	PCWSTR a, b;
	for (; *text; ++text) {
		a = text;
		b = needle;
		while (*b && *a && (insensitive ? Upper(*a) == Upper(*b) : *a == *b)) {
			++a; ++b;
		}
		if (!*b) return text;
	}
	return NULL;
}

#define StringFind(a, b) FindText((a), (b), FALSE)
#define StringFindI(a, b) FindText((a), (b), TRUE)
#define StringSearchI(a, b) (StringFindI((a), (b)) != NULL)
#define wcslen WideLength

static HRESULT CopyWide(PWSTR output, SIZE_T capacity, PCWSTR input, SIZE_T count)
{
	SIZE_T i = 0;
	if (!capacity) return -1;
	while (i + 1 < capacity && i < count && input[i]) {
		output[i] = input[i]; ++i;
	}
	output[i] = 0;
	return (i < count && input[i]) ? -1 : 0;
}

static HRESULT StringCchCopy(PWSTR output, SIZE_T capacity, PCWSTR input)
{
	HRESULT result = CopyWide(output, capacity, input, WideLength(input));
	return FailCopy ? -1 : result;
}

static HRESULT StringCchCopyN(PWSTR output, SIZE_T capacity, PCWSTR input, SIZE_T count)
{
	HRESULT result = CopyWide(output, capacity, input, count);
	return FailCopy ? -1 : result;
}

static int KexCfgMessageBox(HANDLE parent, PCWSTR message, PCWSTR title, ULONG flags)
{
	CopyWide(LastMessage, ARRAYSIZE(LastMessage), message, WideLength(message));
	return 0;
}

static void ExitProcess(NTSTATUS status) __attribute__((noreturn));
static void ExitProcess(NTSTATUS status)
{
	ExitStatus = status;
	longjmp(ExitJump, 1);
}

static HRESULT PathCchCanonicalize(PWSTR output, SIZE_T capacity, PCWSTR input)
{
	if (FailCanonicalize) return -1;
	return CopyWide(output, capacity, input, WideLength(input));
}

static PCWSTR PathFindFileName(PCWSTR path)
{
	PCWSTR name = path;
	for (; *path; ++path) if (*path == L'\\') name = path + 1;
	return name;
}

static PCWSTR PathFindExtension(PCWSTR path)
{
	PCWSTR extension = path + WideLength(path);
	for (path = PathFindFileName(path); *path; ++path) if (*path == L'.') extension = path;
	return extension;
}

static BOOLEAN PathIsPrefix(PCWSTR prefix, PCWSTR path)
{
	SIZE_T n = WideLength(prefix);
	return StringBeginsWithI(path, prefix) && (path[n] == L'\\' || path[n] == 0);
}

static ULONG GetWindowsDirectory(PWSTR output, ULONG capacity)
{
	if (FailWindowsDirectory) return FailWindowsDirectory == 1 ? 0 : capacity + 1;
	CopyWide(output, capacity, L"C:\\Windows", 10);
	return 10;
}

static BOOLEAN KxCfgGetKexDir(PWSTR output, ULONG capacity)
{
	if (FailKexDirectory) return FALSE;
	return CopyWide(output, capacity, L"C:\\Program Files\\VxKex", WideLength(L"C:\\Program Files\\VxKex")) == 0;
}

static BOOLEAN KxCfgGetConfiguration(PCWSTR path, KXCFG_PROGRAM_CONFIGURATION *configuration)
{
	if (!ExistingConfiguration) return FALSE;
	*configuration = Existing;
	return TRUE;
}

static BOOLEAN KxCfgSetConfiguration(PCWSTR path, KXCFG_PROGRAM_CONFIGURATION *configuration,
	HANDLE transaction)
{
	++Applied;
	Written = *configuration;
	CopyWide(WrittenPath, ARRAYSIZE(WrittenPath), path, WideLength(path));
	return TRUE;
}

static HANDLE CreateSimpleTransaction(PCWSTR description) { return (HANDLE) 1; }
static NTSTATUS NtCommitTransaction(HANDLE transaction, BOOLEAN wait) { ++Commits; return 0; }
static NTSTATUS NtRollbackTransaction(HANDLE transaction, BOOLEAN wait) { ++Rollbacks; return 0; }
static void SafeClose(HANDLE transaction) { ++Closes; }
static PCWSTR NtStatusAsString(NTSTATUS status) { return L"test status"; }
static PCWSTR GetLastErrorAsString(void) { return L"test error"; }

static HRESULT StringCchPrintf(PWSTR output, SIZE_T capacity, PCWSTR format, ...)
{
	return CopyWide(output, capacity, format, WideLength(format));
}

/* The platform CRT conversion is a double so ULONG stays 32-bit on Linux LP64. */
static int ScanUnsigned(PCWSTR text, PCWSTR format, ULONG *value)
{
	ULONG digit, result = 0, base = format[2] == L'x' ? 16 : 10;
	int any = 0;
	while (*text == L' ' || *text == L'\t') ++text;
	while (*text) {
		if (*text >= L'0' && *text <= L'9') digit = *text - L'0';
		else if (Upper(*text) >= L'A' && Upper(*text) <= L'F') digit = Upper(*text) - L'A' + 10;
		else break;
		if (digit >= base) break;
		result = result * base + digit;
		any = 1; ++text;
	}
	if (any) *value = result;
	return any;
}
#define swscanf ScanUnsigned

#include "util.c"
#include "cmdline.c"

static void Check(int ok, const char *name)
{
	++Tests;
	if (!ok) { ++Failures; fprintf(stderr, "FAIL: %s\n", name); }
}

static void Reset(void)
{
	ExitStatus = STATUS_SUCCESS;
	Applied = Commits = Rollbacks = Closes = 0;
	FailCopy = FailCanonicalize = 0;
	FailWindowsDirectory = FailKexDirectory = 0;
	ExistingConfiguration = FALSE;
	ZeroMemory(&Existing, sizeof(Existing));
	ZeroMemory(&Written, sizeof(Written));
	WrittenPath[0] = LastMessage[0] = 0;
}

static void RunCommand(PCWSTR command)
{
	if (setjmp(ExitJump) == 0) KexCfgHandleCommandLine(command);
}

static BOOLEAN RunBoolean(PCWSTR value)
{
	if (setjmp(ExitJump) == 0) return KexCfgParseBooleanParameter(value);
	return 2;
}

static void ValidPath(const char *name, PCWSTR command, PCWSTR expected)
{
	Reset();
	RunCommand(command);
	Check(ExitStatus == STATUS_SUCCESS && Applied == 1 && Commits == 1 && Closes == 1 &&
		WideEqual(WrittenPath, expected), name);
}

static void InvalidPath(const char *name, PCWSTR command)
{
	Reset();
	RunCommand(command);
	Check(ExitStatus == STATUS_INVALID_PARAMETER && Applied == 0 && Commits == 0, name);
}

static void TestPaths(void)
{
	WCHAR command[1024], path[MAX_PATH + 2];
	SIZE_T i;
	ValidPath("unquoted executable and later option", L"/EXE:C:\\Apps\\app.exe /ENABLE:1", L"C:\\Apps\\app.exe");
	ValidPath("quoted executable with spaces", L"/EXE:\"C:\\My Apps\\app.exe\" /ENABLE:1", L"C:\\My Apps\\app.exe");
	ValidPath("unquoted executable alone", L"/EXE:C:\\Apps\\app.exe", L"C:\\Apps\\app.exe");
	ValidPath("quoted MSI", L"/EXE:\"D:\\My Apps\\setup.MSI\"", L"D:\\My Apps\\setup.MSI");
	ValidPath("Windows Python exception", L"/EXE:C:\\Windows\\py.exe", L"C:\\Windows\\py.exe");
	InvalidPath("missing EXE", L"/ENABLE:1");
	InvalidPath("empty EXE", L"/EXE:");
	InvalidPath("empty quoted EXE", L"/EXE:\"\"");
	InvalidPath("unterminated quote", L"/EXE:\"C:\\Apps\\app.exe");
	InvalidPath("relative executable", L"/EXE:Apps\\app.exe");
	InvalidPath("UNC path", L"/EXE:\\\\server\\share\\app.exe");
	InvalidPath("unsupported extension", L"/EXE:C:\\Apps\\app.txt");
	InvalidPath("extension without basename", L"/EXE:C:\\Apps\\.exe");
	InvalidPath("Windows directory protected", L"/EXE:C:\\Windows\\notepad.exe");
	InvalidPath("Kex directory protected", L"/EXE:\"C:\\Program Files\\VxKex\\KexCfg.exe\"");

	/* A valid short /EXE must not be measured together with following options. */
	CopyWide(command, ARRAYSIZE(command), L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:3 ", 100);
	i = WideLength(command);
	while (i < 800) command[i++] = L' ';
	command[i] = 0;
	ValidPath("short path with long command tail", command, L"C:\\Apps\\app.exe");

	path[0] = L'C'; path[1] = L':'; path[2] = L'\\';
	for (i = 3; i < MAX_PATH - 5; ++i) path[i] = L'a';
	CopyWide(path + MAX_PATH - 5, 5, L".exe", 4);
	CopyWide(command, ARRAYSIZE(command), L"/EXE:", 5);
	CopyWide(command + 5, ARRAYSIZE(command) - 5, path, WideLength(path));
	ValidPath("maximum supported unquoted path", command, path);
	command[5 + MAX_PATH - 1] = L'X';
	command[5 + MAX_PATH] = 0;
	InvalidPath("overlong path cannot be accepted after truncation", command);

	Reset(); FailCopy = 1;
	RunCommand(L"/EXE:\"C:\\Apps\\app.exe\"");
	Check(ExitStatus == STATUS_INVALID_PARAMETER && Applied == 0, "path copy failure prevents write");
	Reset(); FailCanonicalize = 1;
	RunCommand(L"/EXE:\"C:\\Apps\\app.exe\"");
	Check(ExitStatus == STATUS_INVALID_PARAMETER && Applied == 0, "canonicalization failure prevents write");
	Reset(); FailWindowsDirectory = 1;
	RunCommand(L"/EXE:\"C:\\Apps\\app.exe\"");
	Check(ExitStatus == STATUS_UNSUCCESSFUL && Applied == 0, "Windows directory failure prevents write");
	Reset(); FailWindowsDirectory = 2;
	RunCommand(L"/EXE:\"C:\\Apps\\app.exe\"");
	Check(ExitStatus == STATUS_UNSUCCESSFUL && Applied == 0, "Windows directory overflow prevents write");
	Reset(); FailKexDirectory = 1;
	RunCommand(L"/EXE:\"C:\\Apps\\app.exe\"");
	Check(ExitStatus == STATUS_UNSUCCESSFUL && Applied == 0, "Kex directory failure prevents write");
}

static void TestStrongSpoof(void)
{
	const PCWSTR inputs[] = {
		L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x1",
		L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0X2",
		L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x3 /ENABLE:1",
		L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:3",
		L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x0003",
		L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x0"
	};
	const ULONG expected[] = {1, 2, 3, 3, 3, 0};
	SIZE_T i;
	for (i = 0; i < ARRAYSIZE(inputs); ++i) {
		Reset(); RunCommand(inputs[i]);
		Check(ExitStatus == 0 && Applied == 1 && Written.StrongSpoofOptions == expected[i],
			"strong spoof hexadecimal value preserved");
	}
	InvalidPath("strong spoof invalid mask", L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x4");
	InvalidPath("strong spoof first hex digit cannot be discarded", L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x10");
	InvalidPath("strong spoof missing digits", L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x");
	InvalidPath("strong spoof empty value", L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:");
	InvalidPath("strong spoof malformed value", L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:xyz");

	Reset(); ExistingConfiguration = TRUE;
	Existing.Enabled = TRUE; Existing.DisableForChild = TRUE;
	Existing.WinVerSpoof = WinVerSpoofWin8Point1; Existing.StrongSpoofOptions = 2;
	RunCommand(L"/EXE:\"C:\\Apps\\app.exe\" /STRONGSPOOF:0x1");
	Check(Applied == 1 && Written.Enabled && Written.DisableForChild &&
		Written.WinVerSpoof == WinVerSpoofWin8Point1 && Written.StrongSpoofOptions == 1,
		"unspecified existing settings preserved");
}

static void TestBooleans(void)
{
	const PCWSTR values[] = {L"TRUE", L"YES", L"1", L"FALSE", L"NO", L"0", L"true", L"fAlSe"};
	const BOOLEAN expected[] = {1, 1, 1, 0, 0, 0, 1, 0};
	const PCWSTR invalid[] = {L"", L"TR", L"FAL", L"TRUEextra", L"YESNO", L"2",
		L"TRUE123456789012345678901234567890123456789012345678901234567890"};
	const PCWSTR options[] = {L"/ENABLE:", L"/DISABLEFORCHILD:", L"/DISABLEAPPSPECIFIC:"};
	WCHAR command[512];
	SIZE_T i, j, n;
	BOOLEAN result;
	for (i = 0; i < ARRAYSIZE(values); ++i) {
		Reset(); result = RunBoolean(values[i]);
		Check(ExitStatus == 0 && result == expected[i], "complete boolean value parsed");
		for (j = 0; j < ARRAYSIZE(options); ++j) {
			CopyWide(command, ARRAYSIZE(command), L"/EXE:\"C:\\Apps\\app.exe\" ", 100);
			n = WideLength(command);
			CopyWide(command + n, ARRAYSIZE(command) - n, options[j], WideLength(options[j]));
			n = WideLength(command);
			CopyWide(command + n, ARRAYSIZE(command) - n, values[i], WideLength(values[i]));
			n = WideLength(command);
			CopyWide(command + n, ARRAYSIZE(command) - n, L" /STRONGSPOOF:3", 100);
			Reset(); RunCommand(command);
			result = j == 0 ? Written.Enabled : j == 1 ? Written.DisableForChild : Written.DisableAppSpecificHacks;
			Check(ExitStatus == 0 && Applied == 1 && result == expected[i] && Written.StrongSpoofOptions == 3,
				"boolean CLI option delimited before next option");
		}
	}
	for (i = 0; i < ARRAYSIZE(invalid); ++i) {
		Reset();
		RunBoolean(invalid[i]);
		Check(ExitStatus == STATUS_INVALID_PARAMETER, "invalid or overlong boolean rejected");
		for (j = 0; j < ARRAYSIZE(options); ++j) {
			CopyWide(command, ARRAYSIZE(command), L"/EXE:\"C:\\Apps\\app.exe\" ", 100);
			n = WideLength(command);
			CopyWide(command + n, ARRAYSIZE(command) - n, options[j], WideLength(options[j]));
			n = WideLength(command);
			CopyWide(command + n, ARRAYSIZE(command) - n, invalid[i], WideLength(invalid[i]));
			n = WideLength(command);
			CopyWide(command + n, ARRAYSIZE(command) - n, L" /STRONGSPOOF:3", 100);
			InvalidPath("invalid boolean CLI option prevents write", command);
		}
	}
}

int main(int argc, char **argv)
{
	assert(sizeof(WCHAR) == 2 && sizeof(ULONG) == 4);
	if (argc == 1 || !strcmp(argv[1], "all") || !strcmp(argv[1], "paths")) TestPaths();
	if (argc == 1 || !strcmp(argv[1], "all") || !strcmp(argv[1], "flags")) TestStrongSpoof();
	if (argc == 1 || !strcmp(argv[1], "all") || !strcmp(argv[1], "booleans")) TestBooleans();
	printf("CLI contract checks: %d passed, %d failed\n", Tests - Failures, Failures);
	return Failures ? 1 : 0;
}
