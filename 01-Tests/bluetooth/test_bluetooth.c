#include "mock_windows.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct _BLUETOOTH_DEVICE_SEARCH_PARAMS { int Marker; };
struct _BLUETOOTH_DEVICE_INFO { int Marker; };
struct _BLUETOOTH_FIND_RADIO_PARAMS { int Marker; };

HANDLE BluetoothFindFirstDevice(const struct _BLUETOOTH_DEVICE_SEARCH_PARAMS *,
	struct _BLUETOOTH_DEVICE_INFO *);
BOOL BluetoothFindNextDevice(HANDLE, struct _BLUETOOTH_DEVICE_INFO *);
BOOL BluetoothFindDeviceClose(HANDLE);
HANDLE BluetoothFindFirstRadio(const struct _BLUETOOTH_FIND_RADIO_PARAMS *, HANDLE *);
BOOL BluetoothFindNextRadio(HANDLE, HANDLE *);
BOOL BluetoothFindRadioClose(HANDLE);

static unsigned int LastError;
static int ModuleToken, DeviceToken, RadioToken, RadioOne, RadioTwo;
static int LoadCalls, LookupCalls, NativeCalls[6];
static int LoadFailure, NativeFailure;
static const char *MissingExport;
static struct _BLUETOOTH_DEVICE_SEARCH_PARAMS SearchParams = {11};
static struct _BLUETOOTH_DEVICE_INFO DeviceInfo = {0};
static struct _BLUETOOTH_FIND_RADIO_PARAMS FindParams = {22};
static HANDLE Radio;

static const char *ApiNames[6] = {
	"BluetoothFindFirstDevice", "BluetoothFindNextDevice",
	"BluetoothFindDeviceClose", "BluetoothFindFirstRadio",
	"BluetoothFindNextRadio", "BluetoothFindRadioClose"
};

void SetLastError(unsigned int Error)
{
	LastError = Error;
}

unsigned int GetLastError(void)
{
	return LastError;
}

BOOL InitOnceExecuteOnce(PINIT_ONCE Once, PINIT_ONCE_FN Callback,
	PVOID Parameter, PVOID *Context)
{
	assert(Parameter == NULL && Context == NULL);
	if (Once->Completed) {
		return TRUE;
	}
	if (!Callback(Once, Parameter, Context)) {
		return FALSE;
	}
	Once->Completed = 1;
	return TRUE;
}

HMODULE LoadSystemLibrary(const wchar_t *FileName)
{
	++LoadCalls;
	assert(wcscmp(FileName, L"bthprops.cpl") == 0);
	if (LoadFailure) {
		SetLastError(ERROR_MOD_NOT_FOUND);
		return NULL;
	}
	return &ModuleToken;
}

static BOOL NativeResult(int Index)
{
	++NativeCalls[Index];
	SetLastError((unsigned int) (NativeFailure ? 700 + Index : 900 + Index));
	return !NativeFailure;
}

static HANDLE NativeFirstDevice(const struct _BLUETOOTH_DEVICE_SEARCH_PARAMS *Params,
	struct _BLUETOOTH_DEVICE_INFO *Info)
{
	assert(Params == &SearchParams && Params->Marker == 11 && Info == &DeviceInfo);
	if (!NativeResult(0)) {
		return NULL;
	}
	Info->Marker = 101;
	return &DeviceToken;
}

static BOOL NativeNextDevice(HANDLE Find, struct _BLUETOOTH_DEVICE_INFO *Info)
{
	assert(Find == &DeviceToken && Info == &DeviceInfo);
	if (!NativeResult(1)) {
		return FALSE;
	}
	Info->Marker = 102;
	return TRUE;
}

static BOOL NativeDeviceClose(HANDLE Find)
{
	assert(Find == &DeviceToken);
	return NativeResult(2);
}

static HANDLE NativeFirstRadio(const struct _BLUETOOTH_FIND_RADIO_PARAMS *Params,
	HANDLE *OutRadio)
{
	assert(Params == &FindParams && Params->Marker == 22 && OutRadio == &Radio);
	if (!NativeResult(3)) {
		return NULL;
	}
	*OutRadio = &RadioOne;
	return &RadioToken;
}

static BOOL NativeNextRadio(HANDLE Find, HANDLE *OutRadio)
{
	assert(Find == &RadioToken && OutRadio == &Radio);
	if (!NativeResult(4)) {
		return FALSE;
	}
	*OutRadio = &RadioTwo;
	return TRUE;
}

static BOOL NativeRadioClose(HANDLE Find)
{
	assert(Find == &RadioToken);
	return NativeResult(5);
}

FARPROC GetProcAddress(HMODULE Module, const char *Name)
{
	++LookupCalls;
	assert(Module == &ModuleToken);
	if (MissingExport && strcmp(MissingExport, Name) == 0) {
		SetLastError(ERROR_PROC_NOT_FOUND);
		return NULL;
	}
	if (strcmp(Name, ApiNames[0]) == 0) return (FARPROC) NativeFirstDevice;
	if (strcmp(Name, ApiNames[1]) == 0) return (FARPROC) NativeNextDevice;
	if (strcmp(Name, ApiNames[2]) == 0) return (FARPROC) NativeDeviceClose;
	if (strcmp(Name, ApiNames[3]) == 0) return (FARPROC) NativeFirstRadio;
	if (strcmp(Name, ApiNames[4]) == 0) return (FARPROC) NativeNextRadio;
	if (strcmp(Name, ApiNames[5]) == 0) return (FARPROC) NativeRadioClose;
	assert(!"Unexpected export lookup");
	return NULL;
}

static BOOL CallApi(int Index)
{
	HANDLE Find;
	switch (Index) {
	case 0:
		Find = BluetoothFindFirstDevice(&SearchParams, &DeviceInfo);
		assert(Find == NULL || Find == &DeviceToken);
		return Find != NULL;
	case 1: return BluetoothFindNextDevice(&DeviceToken, &DeviceInfo);
	case 2: return BluetoothFindDeviceClose(&DeviceToken);
	case 3:
		Find = BluetoothFindFirstRadio(&FindParams, &Radio);
		assert(Find == NULL || Find == &RadioToken);
		return Find != NULL;
	case 4: return BluetoothFindNextRadio(&RadioToken, &Radio);
	case 5: return BluetoothFindRadioClose(&RadioToken);
	default: assert(!"Invalid test index"); return FALSE;
	}
}

static void SuccessfulCalls(void)
{
	int Index;
	for (Index = 0; Index < 6; ++Index) {
		SetLastError(12345);
		assert(CallApi(Index));
		assert(GetLastError() == (unsigned int) (900 + Index));
		if (Index == 0) assert(DeviceInfo.Marker == 101);
		if (Index == 1) assert(DeviceInfo.Marker == 102);
		if (Index == 3) assert(Radio == &RadioOne);
		if (Index == 4) assert(Radio == &RadioTwo);
	}
}

int main(int argc, char **argv)
{
	int Index;
	assert(argc == 2);
	if (strcmp(argv[1], "forwarding") == 0) {
		SuccessfulCalls();
		SuccessfulCalls();
		assert(LoadCalls == 1 && LookupCalls == 12);
		for (Index = 0; Index < 6; ++Index) assert(NativeCalls[Index] == 2);
	} else if (strcmp(argv[1], "load-failure-and-retry") == 0) {
		LoadFailure = 1;
		for (Index = 0; Index < 6; ++Index) {
			assert(!CallApi(Index));
			assert(GetLastError() == ERROR_MOD_NOT_FOUND);
			assert(NativeCalls[Index] == 0);
		}
		assert(LoadCalls == 6 && LookupCalls == 0);
		LoadFailure = 0;
		SuccessfulCalls();
		assert(LoadCalls == 7);
	} else if (strcmp(argv[1], "missing-exports") == 0) {
		for (Index = 0; Index < 6; ++Index) {
			MissingExport = ApiNames[Index];
			assert(!CallApi(Index));
			assert(GetLastError() == ERROR_PROC_NOT_FOUND);
			assert(NativeCalls[Index] == 0);
		}
		MissingExport = NULL;
		SuccessfulCalls();
		assert(LoadCalls == 1);
	} else if (strcmp(argv[1], "native-errors") == 0) {
		NativeFailure = 1;
		for (Index = 0; Index < 6; ++Index) {
			assert(!CallApi(Index));
			assert(GetLastError() == (unsigned int) (700 + Index));
			assert(NativeCalls[Index] == 1);
		}
		assert(LoadCalls == 1 && LookupCalls == 6);
	} else {
		assert(!"Unknown test case");
	}
	printf("Bluetooth %s: passed\n", argv[1]);
	return 0;
}
