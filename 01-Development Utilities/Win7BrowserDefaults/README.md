# Brave Nightly defaults on Windows 7

These optional tools repair two separate parts of launching recent Brave Nightly builds on Windows 7 x64:

* `Set-BraveNightlyLaunchCommand.ps1` adds the installed native VxKex launcher before Nightly's original registered command, for the current user.
* `Set-BraveNightlyDefault.ps1` and its adjacent `NightlyDefaults.cs` set only `http`, `https`, `.htm` and `.html` through Windows 7's association API.

They use the existing `Brave Nightly` registration and `BraveSSHTM` ProgID. Stable Brave is outside their scope. The tools read the actual registered browser path and VxKex's `KexDir` installation value; they contain no machine-specific account, address or loader digest.

Recent Nightly builds can have PE OS/subsystem versions of 10.0. A native Windows 7 launch can then fail with error 193 before the browser enters its own code, even with an existing VxKex application profile. The launcher command supplies VxKex's existing process-creation compatibility path. It leaves the original browser path and ` --single-argument %1` tail unchanged.

The browser's own default button is a separate compatibility boundary. Current [Chromium default-app code](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/base/win/default_apps_util.cc) has paths for the modern Windows Settings application and `ms-settings:defaultapps`. A Win10 version profile does not add that UI to Windows 7. This source explains a possible failure path; it is not proof that every Nightly version or button click takes that branch.

## Requirements

Run native 64-bit Windows PowerShell as the Windows user whose defaults you want to change. PowerShell itself must run without VxKex. These scripts require Windows 7, an existing machine registration for Brave Nightly and, for applying the launch command, the existing Nightly VxKex Win10 profile. They read the seven profile parameters and retain them. They do not enable or edit VxKex profiles.

For the launch-command Plan and Apply, provide `ExpectedLoaderSha256` from the trusted loader build you intend to use. A version string or the digest of an arbitrary unverified local file does not establish that trust. The tool compares the installed `VxKexLdr.exe` with that supplied digest. Restore can work from its own backup after Nightly or VxKex has been removed; the old executables do not have to remain installed.

Keep the two PowerShell scripts and the C# source together. Choose an existing private directory for backups and reports, and retain those files locally. They contain the current user's paths, identity and association state. Use a fresh backup/report filename for each Plan or Apply; existing files are never replaced.

## Plan and apply

In a native PowerShell window, set `$expectedHash` to the trusted 64-character SHA256 digest, and `$state` to an existing private directory. Inspect the plan first:

```powershell
.\Set-BraveNightlyLaunchCommand.ps1 -Mode Plan -ExpectedLoaderSha256 $expectedHash
.\Set-BraveNightlyDefault.ps1 -Mode Plan -BackupPath "$state\defaults-plan.xml" -ResultPath "$state\defaults-plan.jsonl"
```

The defaults Plan records native association queries and restoration metadata; it changes no associations. If the previous default cannot be matched to one unique registered application, it refuses to apply rather than promising an unsupported restore.

Apply the launch command before selecting the four defaults:

```powershell
.\Set-BraveNightlyLaunchCommand.ps1 -Mode Apply -ExpectedLoaderSha256 $expectedHash -BackupPath "$state\launch-before.xml"
.\Set-BraveNightlyDefault.ps1 -Mode Apply -BackupPath "$state\defaults-before.xml" -ResultPath "$state\defaults-apply.jsonl"
```

The first tool makes one current-user command-default write, after a CreateNew backup of the relevant keys' existence, values, types and ACLs. It refuses an unrelated user command or an existing `DelegateExecute` value. The second makes at most four individual `SetAppAsDefault` calls and reports their actual HRESULTs. It verifies effective defaults afterward and checks that PDF, SVG and mailto associations did not change. A failed later call can leave earlier successful calls in place; the journal records that partial result.

Open a normal HTTP/HTTPS link and confirm that the actual Nightly browser opens. A successful launcher exit alone is insufficient because the launcher exits before its browser child. No browser, URL or network request is started automatically by these tools.

## Restore

Use the original Apply backups, not a later Plan snapshot. Restore the defaults first, then the launch command:

```powershell
.\Set-BraveNightlyDefault.ps1 -Mode Restore -BackupPath "$state\defaults-before.xml" -ResultPath "$state\defaults-restore.jsonl"
.\Set-BraveNightlyLaunchCommand.ps1 -Mode Restore -BackupPath "$state\launch-before.xml"
```

Command Restore requires the same user and an exact match to the command owned by Apply. It restores the prior default value/type or its absence. It removes only newly created, still-empty keys and does not rewrite ACLs or remove unrelated values.

Defaults Restore uses the saved registered application for each of the four earlier effective handlers. It refuses another subsequently selected browser and requires each previous application still needed for restoration to remain registered with the saved handler. It restores effective associations through the Windows API; it does not recreate the exact former presence or absence of raw `UserChoice` keys. The raw records in the backup are for audit only. Restore is also a sequence of individual operations, with actual results recorded.

The supported [Windows association interface](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iapplicationassociationregistration) differs between Windows 7 and later Windows versions. [SetAppAsDefault](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iapplicationassociationregistration-setappasdefault) can return `E_ACCESSDENIED`; these tools report it without changing security settings. If Windows declines a selection, use Windows 7 Default Programs and choose only the desired associations.

The tools do not use `SetAppAsDefaultAll`, clear all user defaults, write raw `UserChoice` values, register `ms-settings`, alter machine associations, or patch browser binaries.

## Exit codes

The command tool returns `0` on success and `2` on a refused operation or error. The defaults tool returns `0` on success, `2` on admission/backup errors, `10` on a native setter failure, `11` on an effective-default mismatch, and `12` if an unrelated control association changed. Preserve the backups and journal if an operation stops partway through.

## Observed validation

On physical Windows 7 SP1 x64, Brave Nightly 157.1.101.8 could not be
started directly because native process creation returned error 193. After
applying the per-user launcher command and the four native association
selections, opening an HTTPS URL through the normal Windows shell launched
Nightly and rendered the expected page.

The public launch-command tool passed Plan and an Apply/Restore/Apply round
trip. The public defaults wrapper passed Plan and idempotent Apply and Restore
with all four defaults already selected. The earlier equivalent setter made
the actual four selections from Internet Explorer to Nightly and retained the
PDF, SVG and mailto controls. A nontrivial previous-browser restoration with
the public wrapper, and the separate HTTP and local HTML shell-launch cases,
were deferred. These tools do not establish compatibility for arbitrary
browser versions or repair every browser's own default-setting UI.
