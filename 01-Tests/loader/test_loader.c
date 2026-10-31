/* Behavioral tests of the production C routines, without a Windows SDK. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN
#define OUT
#define OPTIONAL
#define STATIC static
#define INLINE
#define NTAPI
#define NTSYSCALLAPI
#define VOID void
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define ASSUME assert
#define NT_SUCCESS(s) ((s) >= 0)
#define STATUS_SUCCESS 0
#define STATUS_BUFFER_TOO_SMALL (-1)
#define STATUS_VARIABLE_NOT_FOUND (-2)
#define STATUS_NO_MEMORY (-3)
#define STATUS_INVALID_PARAMETER (-4)
#define STATUS_PATH_TOO_SHORT (-5)
#define STATUS_ENTRYPOINT_NOT_FOUND (-6)
#define STATUS_TEST_FAILURE (-7)
#define S_OK 0
#define STRSAFE_MAX_CCH ((size_t) 0x7fffffff)
#define OBJ_CASE_INSENSITIVE 0
#define GENERIC_READ 0

typedef int NTSTATUS;
typedef int BOOL;
typedef int INT;
typedef unsigned int ULONG;
typedef unsigned short USHORT;
typedef size_t SIZE_T;
typedef wchar_t WCHAR;
typedef WCHAR *PWCHAR;
typedef WCHAR *PWSTR;
typedef const WCHAR *PCWSTR;
typedef void *PVOID;
typedef PVOID *PPVOID;
typedef PVOID HANDLE;
typedef HANDLE *PHANDLE;
typedef ULONG *PULONG;
typedef ULONG ACCESS_MASK;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } UNICODE_STRING;
typedef UNICODE_STRING *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;
typedef struct { const char *Buffer; } ANSI_STRING;
typedef struct { PUNICODE_STRING ObjectName; } OBJECT_ATTRIBUTES;
typedef OBJECT_ATTRIBUTES *POBJECT_ATTRIBUTES;
typedef struct { UNICODE_STRING DllPath; } TEST_PARAMETERS;
typedef struct { TEST_PARAMETERS *ProcessParameters; } TEST_PEB;
typedef struct { UNICODE_STRING Kex3264DirPath; PVOID SystemDllBase; } TEST_KEX_DATA;

static TEST_PARAMETERS Parameters;
static TEST_PEB Peb = { &Parameters };
static TEST_KEX_DATA Data;
static TEST_KEX_DATA *KexData = &Data;
static PWSTR DriveMap[26];
static WCHAR Environment[32768];
static BOOL EnvironmentExists;
static BOOL NewLoader;
static BOOL NoRemove;
static BOOL TerminateLink;
static INT FailAllocation, Allocations, Outstanding;
static INT FailAppend, Appends;
static BOOL FailQuery, FailQueryValue, FailAdd, FailSet;
static INT Added, Removed, Registered, SetCalls;

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

static void WideCopy(PWSTR destination, PCWSTR source)
{
	memcpy(destination, source, (WideLength(source) + 1) * sizeof(WCHAR));
}

static INT _wcsnicmp(PCWSTR a, PCWSTR b, SIZE_T n)
{
	SIZE_T i;
	for (i = 0; i < n; ++i) {
		if (Upper(a[i]) != Upper(b[i])) return 1;
	}
	return 0;
}

static void *Allocate(SIZE_T bytes)
{
	void *p;
	++Allocations;
	if (FailAllocation && Allocations == FailAllocation) return NULL;
	p = malloc(bytes);
	if (p) { memset(p, 0xa5, bytes); ++Outstanding; }
	return p;
}

static void Release(void *p)
{
	if (p) { --Outstanding; free(p); }
}

#define SafeAlloc(type, count) ((type *) Allocate(sizeof(type) * (count)))
#define SafeFree(p) do { Release(p); (p) = NULL; } while (0)
#define RtlCopyMemory memcpy
#define RtlMoveMemory memmove
#define InitializeObjectAttributes(p, name, flags, root, security) ((p)->ObjectName = (name))
#define VALID_UNICODE_STRING(p) ((p) != NULL)

static TEST_PEB *NtCurrentPeb(void) { return &Peb; }
static PVOID KexLdrGetSystemDllBase(void) { return Data.SystemDllBase; }
static HANDLE GetProcessHeap(void) { return (HANDLE) 1; }
static void *RtlAllocateHeap(HANDLE heap, ULONG flags, SIZE_T bytes) { return Allocate(bytes); }
static BOOL RtlFreeHeap(HANDLE heap, ULONG flags, void *p) { Release(p); return TRUE; }
static NTSTATUS NtClose(HANDLE handle) { return STATUS_SUCCESS; }

static SIZE_T KexRtlUnicodeStringCch(PCUNICODE_STRING s) { return s->Length / sizeof(WCHAR); }
static SIZE_T KexRtlUnicodeStringBufferCch(PCUNICODE_STRING s) { return s->MaximumLength / sizeof(WCHAR); }
static PWSTR KexRtlEndOfUnicodeString(PCUNICODE_STRING s) { return s->Buffer + KexRtlUnicodeStringCch(s); }

static void RtlInitUnicodeString(PUNICODE_STRING s, PCWSTR value)
{
	s->Buffer = (PWSTR) value;
	s->Length = (USHORT) (WideLength(value) * sizeof(WCHAR));
	s->MaximumLength = s->Length + sizeof(WCHAR);
}

static void RtlInitAnsiString(ANSI_STRING *s, const char *value) { s->Buffer = value; }

static NTSTATUS KexRtlNullTerminateUnicodeString(PUNICODE_STRING s)
{
	if (s->Length + sizeof(WCHAR) > s->MaximumLength) return STATUS_BUFFER_TOO_SMALL;
	s->Buffer[s->Length / sizeof(WCHAR)] = 0;
	return STATUS_SUCCESS;
}

static void RtlCopyUnicodeString(PUNICODE_STRING destination, PCUNICODE_STRING source)
{
	USHORT bytes = source->Length < destination->MaximumLength ? source->Length : destination->MaximumLength;
	memmove(destination->Buffer, source->Buffer, bytes);
	destination->Length = bytes;
	KexRtlNullTerminateUnicodeString(destination);
}

static NTSTATUS RtlAppendUnicodeStringToString(PUNICODE_STRING destination, PCUNICODE_STRING source)
{
	++Appends;
	if (FailAppend && Appends == FailAppend) return STATUS_TEST_FAILURE;
	if (destination->Length + source->Length > destination->MaximumLength) return STATUS_BUFFER_TOO_SMALL;
	memcpy(destination->Buffer + destination->Length / sizeof(WCHAR), source->Buffer, source->Length);
	destination->Length += source->Length;
	KexRtlNullTerminateUnicodeString(destination);
	return STATUS_SUCCESS;
}

static NTSTATUS RtlAppendUnicodeToString(PUNICODE_STRING destination, PCWSTR text)
{
	UNICODE_STRING source;
	RtlInitUnicodeString(&source, text);
	return RtlAppendUnicodeStringToString(destination, &source);
}

static BOOL RtlEqualUnicodeString(PCUNICODE_STRING a, PCUNICODE_STRING b, BOOL insensitive)
{
	return a->Length == b->Length && _wcsnicmp(a->Buffer, b->Buffer, a->Length / sizeof(WCHAR)) == 0;
}

static BOOL KexRtlUnicodeStringContainsEmbeddedNull(PCUNICODE_STRING s)
{
	SIZE_T i;
	for (i = 0; i < KexRtlUnicodeStringCch(s); ++i) if (!s->Buffer[i]) return TRUE;
	return FALSE;
}

static NTSTATUS TestAddDirectory(PCUNICODE_STRING path, PPVOID cookie)
{
	++Added;
	if (FailAdd) return STATUS_TEST_FAILURE;
	++Registered;
	*cookie = (PVOID) 1;
	return STATUS_SUCCESS;
}

static NTSTATUS TestRemoveDirectory(PVOID cookie)
{
	++Removed;
	--Registered;
	return STATUS_SUCCESS;
}

static NTSTATUS LdrGetProcedureAddress(PVOID base, ANSI_STRING *name, ULONG ordinal, PPVOID address)
{
	*address = NULL;
	if (!NewLoader) return STATUS_ENTRYPOINT_NOT_FOUND;
	if (!strcmp(name->Buffer, "LdrAddDllDirectory")) *address = (PVOID) TestAddDirectory;
	if (!strcmp(name->Buffer, "LdrRemoveDllDirectory") && !NoRemove) *address = (PVOID) TestRemoveDirectory;
	return *address ? STATUS_SUCCESS : STATUS_ENTRYPOINT_NOT_FOUND;
}

static NTSTATUS RtlQueryEnvironmentVariable(PVOID env, PCWSTR name, SIZE_T nameLength,
	PWSTR output, SIZE_T capacity, SIZE_T *required)
{
	if (FailQuery) return STATUS_TEST_FAILURE;
	if (!EnvironmentExists) return STATUS_VARIABLE_NOT_FOUND;
	*required = WideLength(Environment);
	return *required ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

static NTSTATUS RtlQueryEnvironmentVariable_U(PVOID env, PCUNICODE_STRING name, PUNICODE_STRING value)
{
	SIZE_T bytes = WideLength(Environment) * sizeof(WCHAR);
	if (FailQueryValue) return STATUS_TEST_FAILURE;
	if (bytes > value->MaximumLength) return STATUS_BUFFER_TOO_SMALL;
	memcpy(value->Buffer, Environment, bytes);
	value->Length = (USHORT) bytes;
	KexRtlNullTerminateUnicodeString(value);
	return STATUS_SUCCESS;
}

static NTSTATUS RtlSetEnvironmentVariable(PVOID env, PCUNICODE_STRING name, PCUNICODE_STRING value)
{
	++SetCalls;
	if (FailSet) return STATUS_TEST_FAILURE;
	memcpy(Environment, value->Buffer, value->Length);
	Environment[value->Length / sizeof(WCHAR)] = 0;
	EnvironmentExists = TRUE;
	return STATUS_SUCCESS;
}

NTSTATUS NtOpenSymbolicLinkObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
	SIZE_T drive = attributes->ObjectName->Buffer[4] - L'A';
	if (drive >= 26 || !DriveMap[drive]) return STATUS_VARIABLE_NOT_FOUND;
	*handle = (HANDLE) (drive + 1);
	return STATUS_SUCCESS;
}

NTSTATUS NtQuerySymbolicLinkObject(HANDLE handle, PUNICODE_STRING target, PULONG required)
{
	PCWSTR value = DriveMap[(SIZE_T) handle - 1];
	SIZE_T bytes = WideLength(value) * sizeof(WCHAR);
	*required = (ULONG) bytes;
	if (target->MaximumLength < bytes) return STATUS_BUFFER_TOO_SMALL;
	memcpy(target->Buffer, value, bytes); /* Deliberately not null-terminated. */
	if (TerminateLink && bytes + sizeof(WCHAR) <= target->MaximumLength) {
		target->Buffer[bytes / sizeof(WCHAR)] = 0;
	}
	target->Length = (USHORT) bytes;
	return STATUS_SUCCESS;
}

static INT StringCchLength(PCWSTR text, SIZE_T maximum, SIZE_T *length)
{
	SIZE_T n = 0;
	while (n < maximum && text[n]) ++n;
	*length = n;
	return n == maximum ? -1 : S_OK;
}

static INT StringCchCopy(PWSTR output, SIZE_T capacity, PCWSTR text)
{
	SIZE_T n = WideLength(text);
	if (n >= capacity) return -1;
	WideCopy(output, text);
	return S_OK;
}

static INT StringCchCat(PWSTR output, SIZE_T capacity, PCWSTR text)
{
	SIZE_T n = WideLength(output);
	return StringCchCopy(output + n, capacity - n, text);
}

#include "dllpath.c"
#include "dllnotif.c"

static void Reset(void)
{
	assert(Outstanding == 0);
	memset(&Parameters, 0, sizeof(Parameters));
	memset(DriveMap, 0, sizeof(DriveMap));
	Environment[0] = 0;
	EnvironmentExists = NewLoader = NoRemove = FALSE;
	FailAllocation = Allocations = FailAppend = Appends = 0;
	FailQuery = FailQueryValue = FailAdd = FailSet = FALSE;
	Added = Removed = Registered = SetCalls = 0;
	RtlInitUnicodeString(&Data.Kex3264DirPath, L"C:\\K");
	Data.SystemDllBase = (PVOID) 1;
}

static void SetDllPath(PWSTR buffer, PCWSTR text)
{
	WideCopy(buffer, text);
	RtlInitUnicodeString(&Parameters.DllPath, buffer);
}

static void TestOriginalPathPreserved(INT failure)
{
	WCHAR buffer[256], before[256];
	UNICODE_STRING original;
	Reset();
	memset(buffer, 0x5a, sizeof(buffer));
	SetDllPath(buffer, L"C:/A;C:/A;D:/B;E:/C");
	RtlInitUnicodeString(&Data.Kex3264DirPath, L"C:\\VeryLongKex");
	if (failure == 1 || failure == 2) FailAllocation = failure;
	if (failure >= 3) {
		RtlInitUnicodeString(&Data.Kex3264DirPath, L"C:\\K");
		FailAppend = failure - 2;
	}
	memcpy(before, buffer, sizeof(buffer));
	original = Parameters.DllPath;
	assert(!NT_SUCCESS(KexpAddKex3264ToDllPath()));
	assert(!memcmp(before, buffer, sizeof(buffer)));
	assert(!memcmp(&original, &Parameters.DllPath, sizeof(original)));
	assert(Outstanding == 0);
}

static void TestLegacySuccess(BOOL spareTerminator)
{
	WCHAR buffer[256];
	PCWSTR expected = L"C:\\K;C:\\Alpha;D:\\B;d:\\b";
	USHORT length;
	SIZE_T i;
	Reset();
	SetDllPath(buffer, L"C:/Alpha;D:/B;c:\\alpha;d:\\b");
	length = Parameters.DllPath.Length;
	if (!spareTerminator) {
		Parameters.DllPath.MaximumLength = length;
		buffer[length / sizeof(WCHAR)] = L'!';
	}
	assert(NT_SUCCESS(KexpAddKex3264ToDllPath()));
	assert(Parameters.DllPath.Buffer == buffer && Parameters.DllPath.Length == length);
	assert(!_wcsnicmp(buffer, expected, WideLength(expected)));
	for (i = WideLength(expected); i < length / sizeof(WCHAR); ++i) assert(buffer[i] == L';');
	assert(buffer[length / sizeof(WCHAR)] == (spareTerminator ? 0 : L'!'));
	assert(Outstanding == 0);
}

static void TestEnvironment(INT scenario)
{
	NTSTATUS status;
	WCHAR before[32768];
	Reset();
	NewLoader = TRUE;
	EnvironmentExists = scenario != 0;
	if (scenario >= 2) WideCopy(Environment, L"C:\\Tools;D:\\More");
	WideCopy(before, Environment);
	if (scenario == 3) FailAdd = TRUE;
	if (scenario == 4) FailSet = TRUE;
	if (scenario == 5) FailQuery = TRUE;
	if (scenario == 6) FailQueryValue = TRUE;
	if (scenario == 7) NoRemove = TRUE;
	status = KexpAddKex3264ToDllPath();
	if (scenario < 3) {
		assert(NT_SUCCESS(status));
		assert(!_wcsnicmp(Environment, scenario < 2 ? L"C:\\K" : L"C:\\K;C:\\Tools;D:\\More",
			WideLength(Environment) + 1));
		assert(Added == 1 && Registered == 1 && SetCalls == 1);
	} else {
		assert(!NT_SUCCESS(status));
		assert(!_wcsnicmp(before, Environment, WideLength(before) + 1));
		assert(Registered == 0);
		if (scenario == 4) assert(Removed == 1);
		else assert(SetCalls == 0);
	}
	assert(Outstanding == 0);
}

static void TestLengthBounds(void)
{
	WCHAR buffer[128], before[128];
	SIZE_T i;
	Reset();
	SetDllPath(buffer, L"C:/A;D:/B;E:/C");
	memcpy(before, buffer, sizeof(buffer));
	assert(KexpAddKex3264ToDllPath() == STATUS_PATH_TOO_SHORT);
	assert(!memcmp(before, buffer, sizeof(buffer)));
	Parameters.DllPath.Length = Parameters.DllPath.MaximumLength = 65534;
	assert(KexpAddKex3264ToDllPath() == STATUS_BUFFER_TOO_SMALL);
	assert(!memcmp(before, buffer, sizeof(buffer)));
	assert(Allocations == 2 && Outstanding == 0);

	Reset();
	NewLoader = EnvironmentExists = TRUE;
	for (i = 0; i < 32765; ++i) Environment[i] = L'A';
	Environment[i] = 0;
	assert(KexpAddKex3264ToDllPath() == STATUS_BUFFER_TOO_SMALL);
	assert(Added == 0 && SetCalls == 0 && Outstanding == 0);
	assert(WideLength(Environment) == 32765);
}

static void TestDeviceConversion(BOOL terminatedLink)
{
	UNICODE_STRING path;
	WCHAR *buffer;
	Reset();
	TerminateLink = terminatedLink;
	DriveMap[2] = L"\\Device\\HarddiskVolume2";
	buffer = SafeAlloc(WCHAR, 128);
	WideCopy(buffer, L"\\Device\\HarddiskVolume2\\folder\\test.dll");
	RtlInitUnicodeString(&path, buffer);
	assert(DevicePathToDosPath(&path));
	assert(!_wcsnicmp(path.Buffer, L"C:\\folder\\test.dll", WideLength(L"C:\\folder\\test.dll") + 1));
	SafeFree(path.Buffer);
	assert(Outstanding == 0);
	buffer = SafeAlloc(WCHAR, 2);
	WideCopy(buffer, L"X");
	RtlInitUnicodeString(&path, buffer);
	assert(!DevicePathToDosPath(&path));
	SafeFree(path.Buffer);
	assert(Outstanding == 0);
}

int main(int argc, char **argv)
{
	INT i;
	assert(sizeof(WCHAR) == 2 && sizeof(SIZE_T) == 8);
	if (argc > 1) {
		TestDeviceConversion(TRUE);
		return 0;
	}
	for (i = 0; i <= 5; ++i) TestOriginalPathPreserved(i);
	TestLegacySuccess(TRUE);
	TestLegacySuccess(FALSE);
	for (i = 0; i <= 7; ++i) TestEnvironment(i);
	TestLengthBounds();
	TestDeviceConversion(FALSE);
	TestDeviceConversion(TRUE);
	puts("loader regression tests: PASS (legacy atomicity, environment rollback, x64 device paths)");
	return 0;
}
