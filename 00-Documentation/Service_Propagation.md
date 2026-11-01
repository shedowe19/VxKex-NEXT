# Application profiles and service activation

An enabled process can create direct children with inherited VxKex parameters.
Services are created by the Windows Service Control Manager, so they require an
additional handoff before their process starts.

The service bridge prepares an exact-image IFEO profile for an eligible local
service before an intercepted `StartServiceA/W` call. It also prepares newly
created services after a successful `CreateServiceA/W` and handles registered
COM `LocalService` activation before the native COM call. Service names and
browser product names are not hard-coded. Native Windows performs the actual
activation and supplies its original result and error code.

## Conditions

- The requesting process must already be VxKex-enabled and permit propagation
  to children. A separate UAC or other broker boundary can require its own
  activation configuration before this bridge becomes reachable.
- SCM handle tracking must establish local origin. Explicit remote manager
  names and handles not seen by the bridge retain native behavior.
- The service must be stopped, enabled and have its own Win32 process. Shared
  service hosts and drivers are excluded. No service is automatically stopped
  or restarted.
- The service command must identify an unambiguous existing absolute local
  `.exe` path. Windows-directory targets, including resolved file aliases into
  that directory, are excluded.
- The caller must already have the required query and IFEO-write permissions.
  The bridge never asks for elevation. Additional metadata queries do not
  change the rights requested by the application's original SCM calls.

Failures or unsupported cases fall through to the original native operation.
They do not become fabricated activation successes.

## Configuration semantics

All seven `KEX_IFEO_PARAMETERS` members are persisted: version, strong spoof
options, child and application-specific settings, console enhancements and TLS
protocol masks. The loader reads the same seven registry values.

A native 64-bit registry transaction commits a newly created exact-path filter
only when the target has no existing profile. Existing explicit profiles,
including disabled profiles, win. Basename-wide policies, malformed filters and
dormant filters which would become active are preserved without modification.
A failed transaction leaves no partially installed service profile.

An inherited profile persists as ordinary IFEO configuration and can be managed
through the existing VxKex configuration tools. It applies to that executable
image, including other service registrations or interactive modes using the
same file. This is not a per-request or per-client version setting. A service
which is already running keeps its current configuration until a later start.

The file is held against writes and deletion while preparing its profile, and
its resolved path is checked. This does not eliminate every possible concurrent
parent-directory/junction change or service reconfiguration. Native activation
remains authoritative; no global SCM, RPCSS or kernel patch is installed.

For COM, user class/AppID overrides, class redirection, explicit remote server
registration and caller-specified server information are skipped. This bridge
does not broaden the separate Google Updater 157 COM startup fallback.

## Verification

`01-Tests/services/run-tests.py` compiles actual production functions with
explicit OS doubles. The SDK runner covers both x86 and x64. Tests exercise
handle provenance, original access/arguments/errors, profile eligibility,
path and resource handling, complete parameter forwarding, configuration
transactions and COM routing. These checks do not alone establish behavior in
an actual SCM-created service; an isolated native own-service fixture provides
the separate runtime check.
