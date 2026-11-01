# UAC profile propagation contracts

Run `python 01-Tests/uac/run-tests.py` with a C compiler on PATH. Use
`--compiler cl` in a Visual C++ 2010 / Windows 7.1 SDK command environment,
or `--compiler cc` for the portable behavior checks. The runner compiles four
temporary executables and runs each one as a subprocess. It never launches an
installer, prompts for elevation, opens a real pipe or modifies a process.

The runner extracts the current production functions rather than maintaining a
second implementation. It also extracts the actual profile enum/structure from
`KexDll.h`. Windows builds use real SDK shell/process types and `WINAPI` calling
conventions; they check the 16-byte header, 32-byte message, 28-byte profile and
66,680-byte request layouts. Portable builds use the host `wchar_t` and verify
behavior, not the Windows UTF-16 wire layout.

- Protocol tests exercise the shared request/header/string/profile/path
  validators and actual helper nonce/PID/time parsers. They reject malformed
  lengths, embedded NULs, caller identity mismatches, unsupported show modes,
  overlapping TLS masks and oversized command lines.
- Client tests compile the actual `KxuExecute` and four public shell wrappers.
  API doubles cover native fall-through, the original A/W function and last
  error, consent cancellation, peer identity, the exact seven profile fields,
  limited target access (`0x00101000`), ACK and STARTED validation, handle
  ownership and failures before/after consent. Expensive admission helpers
  (token, IFEO, file pinning and process identity) are explicitly doubled; these
  tests do not establish real Windows permission or file-identity behavior.
- Helper tests compile the actual `KxuElevateMain`. They check the identification
  SQOS flag, profile assignment before propagation/CPIW initialization,
  suspended original-child creation, exact ACK before resume and cleanup of
  owned unresumed children. A zero previous suspend count never kills a target;
  failures after a successful resume never kill it either. Caller-token and
  image-pin helpers and native I/O remain API doubles.
- Core tests exercise the actual name/range/data-slot validators and dynamic
  native-pointer mapper. They check section bounds, overlapping executable
  sections, ordinal/native-pointer rejection, exact API names, loader-lock
  failure and last-error preservation. They do not execute the complete loaded
  module scanner or alter an IAT.

Every newly used native call in an extracted entry point must have an explicit
test double before the suite will compile it. A passing suite establishes these
contracts; it does not establish that native UAC, the pipe or the loader works
on a live Windows 7 desktop.

## Native Windows 7 acceptance fixture

Use a disposable test account with a split administrator token and start a
harmless fixture from its normal Medium desktop. The fixture should request
normal `runas` for a separate probe executable with OS/subsystem version 10.0.
Use an argument nonce, a known current directory and a finite diagnostic exit
code. Enable a valid distinct NEXT profile exercising all seven fields; the
target must have no explicit IFEO policy of its own. Do not use an installer for
this acceptance check.

The result must independently show:

1. The original caller remains Medium, a real native consent prompt appears,
   and the child token is High with the same user SID and session.
2. The child's actual process image, arguments, directory, subsystem header and
   seven effective profile values match the requested probe. Confirm the child
   is a separate original process, not the helper.
3. With `SEE_MASK_NOCLOSEPROCESS`, the returned handle identifies that child's
   PID, creation time and image. `WaitForSingleObject` and `GetExitCodeProcess`
   on that handle must return the probe's actual termination/diagnostic result.
   Query the handle's granted access and confirm it is limited to synchronize
   and limited query, without process-write, token or duplicate-handle access.
4. Cancelling native consent returns failure/`ERROR_CANCELLED`, with no child
   and no second consent prompt. Unsupported/explicitly configured targets and
   ordinary standard-user requests retain native behavior.

Repeat for x86 and x64 callers/children as supported by the installed package.
Only after this fixture succeeds should a signed application installer be used
to check that its administrator child retains the profile and reaches its next
real installation stage. A request reaching a server is distinct from a
successful download, inner installation and working browser window.
