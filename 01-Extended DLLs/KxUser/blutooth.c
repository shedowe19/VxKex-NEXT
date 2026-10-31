#include "buildcfg.h"
#include "kxuserp.h"

//
// Adapted from ArchitectOfRuin's classic Bluetooth wrappers in upstream PR #470:
// https://github.com/YuZhouRen86/VxKex-NEXT/pull/470
//
// BluetoothAPIs.h in the Windows 7.1 SDK contains malformed pragma directives.
// Only opaque structure pointers are needed here, so avoid including that header
// in this /WX build. Enumeration handles are ordinary HANDLE-sized values.
//

typedef struct _BLUETOOTH_DEVICE_SEARCH_PARAMS BLUETOOTH_DEVICE_SEARCH_PARAMS;
typedef struct _BLUETOOTH_DEVICE_INFO BLUETOOTH_DEVICE_INFO;
typedef struct _BLUETOOTH_FIND_RADIO_PARAMS BLUETOOTH_FIND_RADIO_PARAMS;
typedef HANDLE HBLUETOOTH_DEVICE_FIND;
typedef HANDLE HBLUETOOTH_RADIO_FIND;

STATIC INIT_ONCE BthPropsInitOnce = INIT_ONCE_STATIC_INIT;
STATIC HMODULE BthPropsModule;

STATIC BOOL CALLBACK InitializeBthProps(
	PINIT_ONCE InitOnce,
	PVOID Parameter,
	PVOID *Context)
{
	UNREFERENCED_PARAMETER(InitOnce);
	UNREFERENCED_PARAMETER(Parameter);
	UNREFERENCED_PARAMETER(Context);

	// Load the system module without rewriting its own imports back to KxUser.
	// Keep the reference for the lifetime of the enumeration handles it creates.
	BthPropsModule = LoadSystemLibrary(L"bthprops.cpl");
	return BthPropsModule != NULL;
}

STATIC HMODULE GetBthPropsModule(
	VOID)
{
	// Failed initialization is retryable, for example after a transient load error.
	if (!InitOnceExecuteOnce(&BthPropsInitOnce, InitializeBthProps, NULL, NULL)) {
		return NULL;
	}

	return BthPropsModule;
}

KXUSERAPI HBLUETOOTH_DEVICE_FIND WINAPI BluetoothFindFirstDevice(
	IN const BLUETOOTH_DEVICE_SEARCH_PARAMS *SearchParams,
	OUT BLUETOOTH_DEVICE_INFO *DeviceInfo)
{
	typedef HBLUETOOTH_DEVICE_FIND (WINAPI *PFN_BLUETOOTH_FIND_FIRST_DEVICE)(
		const BLUETOOTH_DEVICE_SEARCH_PARAMS *, BLUETOOTH_DEVICE_INFO *);
	HMODULE Module;
	PFN_BLUETOOTH_FIND_FIRST_DEVICE Function;

	Module = GetBthPropsModule();
	if (!Module) {
		return NULL;
	}

	Function = (PFN_BLUETOOTH_FIND_FIRST_DEVICE) GetProcAddress(Module, "BluetoothFindFirstDevice");
	if (!Function) {
		return NULL;
	}

	return Function(SearchParams, DeviceInfo);
}

KXUSERAPI BOOL WINAPI BluetoothFindNextDevice(
	IN HBLUETOOTH_DEVICE_FIND FindHandle,
	OUT BLUETOOTH_DEVICE_INFO *DeviceInfo)
{
	typedef BOOL (WINAPI *PFN_BLUETOOTH_FIND_NEXT_DEVICE)(
		HBLUETOOTH_DEVICE_FIND, BLUETOOTH_DEVICE_INFO *);
	HMODULE Module;
	PFN_BLUETOOTH_FIND_NEXT_DEVICE Function;

	Module = GetBthPropsModule();
	if (!Module) {
		return FALSE;
	}

	Function = (PFN_BLUETOOTH_FIND_NEXT_DEVICE) GetProcAddress(Module, "BluetoothFindNextDevice");
	if (!Function) {
		return FALSE;
	}

	return Function(FindHandle, DeviceInfo);
}

KXUSERAPI BOOL WINAPI BluetoothFindDeviceClose(
	IN HBLUETOOTH_DEVICE_FIND FindHandle)
{
	typedef BOOL (WINAPI *PFN_BLUETOOTH_FIND_DEVICE_CLOSE)(HBLUETOOTH_DEVICE_FIND);
	HMODULE Module;
	PFN_BLUETOOTH_FIND_DEVICE_CLOSE Function;

	Module = GetBthPropsModule();
	if (!Module) {
		return FALSE;
	}

	Function = (PFN_BLUETOOTH_FIND_DEVICE_CLOSE) GetProcAddress(Module, "BluetoothFindDeviceClose");
	if (!Function) {
		return FALSE;
	}

	return Function(FindHandle);
}

KXUSERAPI HBLUETOOTH_RADIO_FIND WINAPI BluetoothFindFirstRadio(
	IN const BLUETOOTH_FIND_RADIO_PARAMS *FindParams,
	OUT HANDLE *Radio)
{
	typedef HBLUETOOTH_RADIO_FIND (WINAPI *PFN_BLUETOOTH_FIND_FIRST_RADIO)(
		const BLUETOOTH_FIND_RADIO_PARAMS *, HANDLE *);
	HMODULE Module;
	PFN_BLUETOOTH_FIND_FIRST_RADIO Function;

	Module = GetBthPropsModule();
	if (!Module) {
		return NULL;
	}

	Function = (PFN_BLUETOOTH_FIND_FIRST_RADIO) GetProcAddress(Module, "BluetoothFindFirstRadio");
	if (!Function) {
		return NULL;
	}

	return Function(FindParams, Radio);
}

KXUSERAPI BOOL WINAPI BluetoothFindNextRadio(
	IN HBLUETOOTH_RADIO_FIND FindHandle,
	OUT HANDLE *Radio)
{
	typedef BOOL (WINAPI *PFN_BLUETOOTH_FIND_NEXT_RADIO)(HBLUETOOTH_RADIO_FIND, HANDLE *);
	HMODULE Module;
	PFN_BLUETOOTH_FIND_NEXT_RADIO Function;

	Module = GetBthPropsModule();
	if (!Module) {
		return FALSE;
	}

	Function = (PFN_BLUETOOTH_FIND_NEXT_RADIO) GetProcAddress(Module, "BluetoothFindNextRadio");
	if (!Function) {
		return FALSE;
	}

	return Function(FindHandle, Radio);
}

KXUSERAPI BOOL WINAPI BluetoothFindRadioClose(
	IN HBLUETOOTH_RADIO_FIND FindHandle)
{
	typedef BOOL (WINAPI *PFN_BLUETOOTH_FIND_RADIO_CLOSE)(HBLUETOOTH_RADIO_FIND);
	HMODULE Module;
	PFN_BLUETOOTH_FIND_RADIO_CLOSE Function;

	Module = GetBthPropsModule();
	if (!Module) {
		return FALSE;
	}

	Function = (PFN_BLUETOOTH_FIND_RADIO_CLOSE) GetProcAddress(Module, "BluetoothFindRadioClose");
	if (!Function) {
		return FALSE;
	}

	return Function(FindHandle);
}

//
// These are all stubs. No idea how to begin implementing them properly.
// Let's see if they are even that important.
//

KXUSERAPI HRESULT WINAPI BluetoothGATTAbortReliableWrite(
	IN	HANDLE	Device,
	IN	ULONG	ReliableWriteContext,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTBeginReliableWrite(
	IN	HANDLE	Device,
	OUT	PVOID	ReliableWriteContext,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTEndReliableWrite(
	IN	HANDLE	Device,
	IN	ULONG	ReliableWriteContext,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTGetCharacteristics(
	IN	HANDLE	Device,
	IN	PVOID	Service OPTIONAL,
	IN	USHORT	CharacteristicsBufferCount,
	OUT	PVOID	CharacteristicsBuffer OPTIONAL,
	OUT	PUSHORT	CharacteristicsBufferActual,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTGetCharacteristicValue(
	IN	HANDLE	Device,
	IN	PVOID	Characteristic,
	IN	ULONG	CharacteristicValueDataSize,
	OUT	PVOID	CharacteristicValue OPTIONAL,
	OUT	PUSHORT	CharacteristicValueSizeRequired OPTIONAL,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTGetDescriptors(
	IN	HANDLE	Device,
	IN	PVOID	Characteristic,
	IN	USHORT	DescriptorsBufferCount,
	OUT	PVOID	DescriptorsBuffer OPTIONAL,
	OUT	PUSHORT	DescriptorsBufferActual,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTGetDescriptorValue(
	IN	HANDLE	Device,
	IN	PVOID	Descriptor,
	IN	ULONG	DescriptorValueDataSize,
	OUT	PVOID	DescriptorValue OPTIONAL,
	OUT	PUSHORT	DescriptorValueSizeRequired OPTIONAL,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTGetIncludedServices(
	IN	HANDLE	Device,
	IN	PVOID	ParentService OPTIONAL,
	IN	USHORT	IncludedServicesBufferCount,
	OUT	PVOID	IncludedServicesBuffer OPTIONAL,
	OUT	PUSHORT	IncludedServicesBufferActual,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTGetServices(
	IN	HANDLE	Device,
	IN	USHORT	ServicesBufferCount,
	OUT	PVOID	ServicesBuffer OPTIONAL,
	OUT	PUSHORT	ServicesBufferActual,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTRegisterEvent(
	IN	HANDLE	Service,
	IN	ULONG	EventType,
	IN	PVOID	EventParameterIn,
	IN	PVOID	Callback,
	IN	PVOID	CallbackContext OPTIONAL,
	OUT	PVOID	EventHandle,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTSetCharacteristicValue(
	IN	HANDLE	Device,
	IN	PVOID	Characteristic,
	IN	PVOID	CharacteristicValue,
	IN	PVOID	ReliableWriteContext OPTIONAL,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTSetDescriptorValue(
	IN	HANDLE	Device,
	IN	PVOID	Descriptor,
	IN	PVOID	DescriptorValue,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}

KXUSERAPI HRESULT WINAPI BluetoothGATTUnregisterEvent(
	IN	HANDLE	EventHandle,
	IN	ULONG	Flags)
{
	return ERROR_NOT_SUPPORTED;
}
