# Configuration-manager notification backport

In an MSVC/Windows SDK command environment, run:

```
python run-tests.py
```

The test extracts the actual `KxBase/cfgmgr.c`, compiles it, and exercises a real
legacy `RegisterDeviceNotificationW` registration, worker thread, and hidden
message window. Device-change test messages are synthetic and sent only to the
test's owned window. They do not attach, remove, reset, or otherwise change a
physical device. No claim of physical PnP-event delivery follows from this test.

Checks include interface arrival/removal payloads and filtering, handle event
translation and custom binary data, query-remove cancellation, pending callback
completion before unregister returns, concurrent/stale/self-unregister handling,
initialization failure before creation of a message queue, invalid arguments,
and repeated registration cleanup without a kernel-handle leak. The native
Windows11 reference and physical Windows7 integration probe are separate tests.
The stopping state must still dispatch non-device teardown messages through
DefWindowProc; a real WM_NCDESTROY dispatch check covers that lifetime contract.

The current suite has 113 assertions. A physical Win7 run exposed that a failed
legacy DEVICEHANDLE registration reports error 1066 for NULL, event and NUL-device
targets, whereas the native modern CM reference returns CR_INVALID_DATA. NULL
and INVALID_HANDLE_VALUE targets now fail before creating a worker. The remaining
empirical 1066 translation applies only after RegisterDeviceNotificationW itself
fails for a DEVICEHANDLE filter; the original Win32 error is retained. Interface
registration, class/window creation and user32 initialization with that same
error remain CR_FAILURE. Injected failures distinguish each context and preserve
the original cleanup/lifetime checks. Nondevice-handle rejection now requires
exact CR_INVALID_DATA, rather than accepting any failure.

Error 1066 is a generic service-specific code, so the legacy result cannot
distinguish an otherwise-valid device handle's unrelated service failure. This
limited translation is not a claim of recovering every underlying native CM
status or of successful device registration. No new handle/device I/O probes,
fake successful registration or weakened status expectations were added.

The backport dynamically resolves user32 only when registration is requested.
It implements interface-class filters (including all classes) and legacy device
handle filters. Device-instance filters remain `CR_CALL_NOT_IMPLEMENTED` because
the legacy API does not provide the same enumeration/start/removal contract.
Do not call unregister synchronously from a callback: this is forbidden by the
native contract; this implementation returns `CR_FAILURE` for that misuse.

Real physical device-handle registration, delivery of actual PnP events, and
noninteractive/restricted-desktop operation still require integration evidence.
Synthetic handle payload tests do not establish those capabilities.

Primary references:

- [CM_Register_Notification](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_register_notification)
- [CM_Unregister_Notification](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_unregister_notification)
- [CM_NOTIFY_FILTER](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/ns-cfgmgr32-cm_notify_filter)
- [CM_NOTIFY_EVENT_DATA](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/ns-cfgmgr32-cm_notify_event_data)
- [RegisterDeviceNotificationW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerdevicenotificationw)
- [DEV_BROADCAST_HANDLE](https://learn.microsoft.com/en-us/windows/win32/api/dbt/ns-dbt-dev_broadcast_handle)
- [WM_NCDESTROY](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncdestroy)
- [System error codes 1000–1299](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1000-1299-)
