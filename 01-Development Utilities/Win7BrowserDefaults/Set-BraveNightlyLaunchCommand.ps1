param(
    [ValidateSet('Plan','Apply','Restore')][string]$Mode = 'Plan',
    [string]$BackupPath,
    [string]$ExpectedLoaderSha256
)
# Windows 7 / PowerShell 2. Changes only the current user's BraveSSHTM open default value.
$ErrorActionPreference = 'Stop'
$nightly = $null
$loader = $null
$loaderSha = $ExpectedLoaderSha256
$commandPath = 'Software\Classes\BraveSSHTM\shell\open\command'
$paths = @('Software\Classes\BraveSSHTM','Software\Classes\BraveSSHTM\shell',
    'Software\Classes\BraveSSHTM\shell\open',$commandPath)
$expectedMachine = $null
$ownedCommand = $null
$hcu = [Microsoft.Win32.Registry]::CurrentUser
$hlm = [Microsoft.Win32.Registry]::LocalMachine
$rawOptions = [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames

function Say([string]$text) { [Console]::Out.WriteLine($text) }
function HasValue($key,[string]$name) {
    if ($null -eq $key) { return $false }
    foreach ($n in $key.GetValueNames()) { if ($n -ceq $name) { return $true } }
    return $false
}
function ValueRecord($key,[string]$name) {
    $exists = HasValue $key $name
    $kind = $null; $data = $null
    if ($exists) { $kind = [string]$key.GetValueKind($name); $data = $key.GetValue($name,$null,$rawOptions) }
    return (New-Object PSObject -Property @{ Name=$name; Exists=$exists; Kind=$kind; Data=$data })
}
function Snapshot([string]$path) {
    $key = $hcu.OpenSubKey($path,$false)
    try {
        $values = @(); $children = @(); $sddl = $null
        if ($null -ne $key) {
            foreach ($n in $key.GetValueNames()) { $values += (ValueRecord $key $n) }
            $children = @($key.GetSubKeyNames())
            $parts = [System.Security.AccessControl.AccessControlSections]::Owner -bor
                [System.Security.AccessControl.AccessControlSections]::Group -bor
                [System.Security.AccessControl.AccessControlSections]::Access
            $sddl = $key.GetAccessControl($parts).GetSecurityDescriptorSddlForm($parts)
        }
        return (New-Object PSObject -Property @{ Path=$path; Exists=($null -ne $key);
            Values=$values; SubKeys=$children; Sddl=$sddl })
    } finally { if ($null -ne $key) { $key.Close() } }
}
function CurrentDefault {
    $key = $hcu.OpenSubKey($commandPath,$false)
    try { return (ValueRecord $key '') }
    finally { if ($null -ne $key) { $key.Close() } }
}
function HashFile([string]$path) {
    $stream = [IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $stream.Close(); $hash.Clear() }
}

function InitializeRestorePaths($saved) {
    if ($saved.Schema -ne 'VxKexNightlyLoaderCommand1' -or $saved.Sid -cne $sid -or $saved.CommandPath -cne $commandPath) { throw 'Backup identity/target mismatch.' }
    $exe = [string]$saved.NightlyPath; $ldr = [string]$saved.LoaderPath
    if ($exe -notmatch '^[A-Za-z]:\\[^"\r\n]+\\brave\.exe$' -or $ldr -notmatch '^[A-Za-z]:\\[^"\r\n]+\\VxKexLdr\.exe$') { throw 'Invalid saved executable paths.' }
    $original = '"' + $exe + '" --single-argument %1'
    $owned = '"' + $ldr + '" ' + $original
    if ([string]$saved.MachineCommand -cne $original -or [string]$saved.OwnedCommand -cne $owned -or [string]$saved.LoaderSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid saved command construction.' }
    if (-not [string]::IsNullOrEmpty($ExpectedLoaderSha256) -and $ExpectedLoaderSha256 -ine [string]$saved.LoaderSha256) { throw 'ExpectedLoaderSha256 differs from the saved build.' }
    $script:nightly = $exe; $script:loader = $ldr; $script:loaderSha = [string]$saved.LoaderSha256
    $script:expectedMachine = $original; $script:ownedCommand = $owned
}
function InitializeInstalledPaths {
    if ($ExpectedLoaderSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Provide ExpectedLoaderSha256 from the trusted loader build you intend to use.' }
    $script:loaderSha = $ExpectedLoaderSha256.ToLowerInvariant()
    $registeredApps = $hlm.OpenSubKey('Software\RegisteredApplications',$false)
    $userApps = $hcu.OpenSubKey('Software\RegisteredApplications',$false)
    try {
        if (HasValue $userApps 'Brave Nightly') { throw 'A per-user Nightly registration requires separate review; no change made.' }
        if (-not (HasValue $registeredApps 'Brave Nightly')) { throw 'Machine Brave Nightly registration was not found.' }
        $capabilities = [string]$registeredApps.GetValue('Brave Nightly','',$rawOptions)
        if ($capabilities -notmatch '^Software\\Clients\\StartMenuInternet\\[^\\]+\\Capabilities$') { throw 'Unexpected Nightly capabilities path.' }
    } finally {
        if ($null -ne $userApps) { $userApps.Close() }
        if ($null -ne $registeredApps) { $registeredApps.Close() }
    }
    foreach ($association in @('http','https','.htm','.html')) {
        $group = 'FileAssociations'; if ($association -eq 'http' -or $association -eq 'https') { $group = 'URLAssociations' }
        $key = $hlm.OpenSubKey(($capabilities + '\' + $group),$false)
        try {
            if ($null -eq $key -or [string]$key.GetValue($association,'',$rawOptions) -cne 'BraveSSHTM') { throw 'Nightly must register exactly BraveSSHTM for all four selected associations.' }
        } finally { if ($null -ne $key) { $key.Close() } }
    }
    $key = $hlm.OpenSubKey($commandPath,$false)
    try {
        $original = ValueRecord $key ''
        if (-not $original.Exists -or $original.Kind -ne 'String') { throw 'The machine Nightly command must be REG_SZ.' }
        $raw = [string]$original.Data
        if ($raw -notmatch '^"([A-Za-z]:\\[^"\r\n]+\\brave\.exe)"( --single-argument %1)$') { throw 'The registered Nightly command differs from the supported quoted EXE and unchanged --single-argument %1 tail.' }
        if ([string]$Matches[2] -cne ' --single-argument %1') { throw 'The original command tail has an unexpected spelling.' }
        $script:nightly = [string]$Matches[1]
        $script:expectedMachine = $raw
    } finally { if ($null -ne $key) { $key.Close() } }
    if (-not [IO.File]::Exists($nightly)) { throw 'The registered Nightly executable is missing.' }
    $product = [Diagnostics.FileVersionInfo]::GetVersionInfo($nightly)
    if ($product.ProductName -notmatch '(?i)Brave.*Nightly') { throw 'The registered executable is not identified as Brave Nightly.' }
    $installKey = $hlm.OpenSubKey('Software\VXsoft\VxKex',$false)
    if ($null -eq $installKey) { $installKey = $hlm.OpenSubKey('Software\VXsoft\VxKexLdr',$false) }
    try {
        if ($null -eq $installKey) { throw 'VxKex installation metadata was not found.' }
        $dir = ValueRecord $installKey 'KexDir'
        if (-not $dir.Exists -or ($dir.Kind -ne 'String' -and $dir.Kind -ne 'ExpandString')) { throw 'VxKex KexDir is missing or has an unexpected type.' }
        $path = [string]$dir.Data
        if ($dir.Kind -eq 'ExpandString') { $path = [Environment]::ExpandEnvironmentVariables($path) }
        if ($path -notmatch '^[A-Za-z]:\\' -or $path.IndexOf('"') -ge 0) { throw 'KexDir must be an absolute local directory.' }
        $script:loader = [IO.Path]::Combine([IO.Path]::GetFullPath($path),'VxKexLdr.exe')
    } finally { if ($null -ne $installKey) { $installKey.Close() } }
    $script:ownedCommand = '"' + $loader + '" ' + $expectedMachine
    Say ('RegisteredApp=Brave Nightly; Capabilities=' + $capabilities)
    Say ('NightlyVersion=' + $product.FileVersion)
    Say ('Loader=' + $loader + '; ExpectedSHA256=' + $loaderSha)
}

function WriteNewBackup($object,[string]$path) {
    if ([string]::IsNullOrEmpty($path) -or -not [IO.Path]::IsPathRooted($path)) { throw 'An absolute fresh BackupPath is required.' }
    $full = [IO.Path]::GetFullPath($path); $parent = [IO.Path]::GetDirectoryName($full)
    if (-not [IO.Directory]::Exists($parent)) { throw 'Backup parent directory must already exist.' }
    $temp = [IO.Path]::Combine($parent,('NightlyCommand-' + [Guid]::NewGuid().ToString('N') + '.tmp.xml'))
    $sourceStream = $null; $backupStream = $null
    try {
        $object | Export-Clixml -Path ([System.Management.Automation.WildcardPattern]::Escape($temp)) -Depth 12
        $sourceStream = [IO.File]::OpenRead($temp)
        # CreateNew refuses to replace any existing backup, including an empty one.
        $backupStream = New-Object IO.FileStream($full,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $bytes = New-Object byte[] 8192
        while (($count = $sourceStream.Read($bytes,0,$bytes.Length)) -gt 0) { $backupStream.Write($bytes,0,$count) }
        $backupStream.Flush(); $backupStream.Close(); $backupStream = $null
    } finally {
        if ($null -ne $backupStream) { $backupStream.Close() }; if ($null -ne $sourceStream) { $sourceStream.Close() }
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
    }
    return $full
}
function NoDelegateExecute {
    foreach ($base in @($hcu,$hlm)) {
        foreach ($path in @($commandPath,'Software\Classes\BraveSSHTM\shell\open')) {
            $key = $base.OpenSubKey($path,$false)
            try { if (HasValue $key 'DelegateExecute') { throw ('DelegateExecute is present at ' + $base.Name + '\' + $path + '; no change made.') } }
            finally { if ($null -ne $key) { $key.Close() } }
        }
    }
}
function CheckNightlyIfeo {
    $path = 'SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\brave.exe'
    $key = $hlm.OpenSubKey($path,$false); $matches = @()
    try {
        if ($null -eq $key) { throw 'Nightly IFEO key is absent.' }
        Say ('IFEO.UseFilter=' + $key.GetValue('UseFilter','<absent>',$rawOptions))
        if ([int]$key.GetValue('UseFilter',0,$rawOptions) -ne 1) { throw 'IFEO UseFilter must be 1.' }
        foreach ($name in $key.GetSubKeyNames()) {
            $filter = $key.OpenSubKey($name,$false)
            try { if ([string]::Equals([string]$filter.GetValue('FilterFullPath','',$rawOptions),$nightly,[StringComparison]::OrdinalIgnoreCase)) { $matches += $name } }
            finally { if ($null -ne $filter) { $filter.Close() } }
        }
        if ($matches.Count -ne 1) { throw 'Exactly one IFEO filter must match the Nightly executable.' }
        $filter = $key.OpenSubKey($matches[0],$false)
        try {
            Say ('IFEO.Filter=' + $path + '\' + $matches[0])
            $names = @('GlobalFlag','VerifierDlls','VerifierFlags','KEX_DisableForChild','KEX_DisableAppSpecific',
                'KEX_WinVerSpoof','KEX_StrongVersionSpoof','KEX_DisableConsoleEnhancements',
                'KEX_TlsForceEnabledProtocols','KEX_TlsForceDisabledProtocols')
            foreach ($name in $names) {
                $v = ValueRecord $filter $name
                if ($v.Exists) { Say ('IFEO.' + $name + ' [' + $v.Kind + ']=' + [string]$v.Data) }
                else { Say ('IFEO.' + $name + '=<absent; KEX parameter default is zero>') }
            }
            if (([int]$filter.GetValue('GlobalFlag',0,$rawOptions) -band 256) -eq 0) { throw 'Nightly IFEO application verifier is not enabled.' }
            if ([string]$filter.GetValue('VerifierDlls','',$rawOptions) -notmatch '(?i)(^|[\s;,])kexdll\.dll($|[\s;,])') { throw 'Nightly IFEO kexdll.dll is absent.' }
            if ([int]$filter.GetValue('KEX_WinVerSpoof',0,$rawOptions) -ne 4) { throw 'Nightly IFEO must retain the user-selected Win10 profile (4).' }
            if ([int]$filter.GetValue('KEX_StrongVersionSpoof',0,$rawOptions) -ne 0) { throw 'Unexpected Nightly StrongVersionSpoof; no change made.' }
            if ([int]$filter.GetValue('KEX_DisableForChild',0,$rawOptions) -ne 0) { throw 'Unexpected Nightly DisableForChild; no change made.' }
        } finally { $filter.Close() }
    } finally { if ($null -ne $key) { $key.Close() } }
}

try {
    if ([IntPtr]::Size -ne 8 -or [Environment]::OSVersion.Version.Major -ne 6 -or [Environment]::OSVersion.Version.Minor -ne 1) { throw 'Use native 64-bit PowerShell on the Windows 7 PC.' }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if ($identity.IsSystem -or $identity.IsAnonymous -or $null -eq $identity.User) { throw 'Run as the Windows user whose browser defaults are to be changed, not a service account.' }
    foreach ($module in [Diagnostics.Process]::GetCurrentProcess().Modules) {
        if ($module.ModuleName -ieq 'kexdll.dll') { throw 'Run this administration script without VxKex.' }
    }
    $sid = $identity.User.Value
    if ($Mode -eq 'Restore') {
        if ([string]::IsNullOrEmpty($BackupPath) -or -not [IO.Path]::IsPathRooted($BackupPath)) { throw 'Restore requires the absolute original BackupPath.' }
        $saved = Import-Clixml -Path ([System.Management.Automation.WildcardPattern]::Escape($BackupPath))
        InitializeRestorePaths $saved
        if ($saved.Schema -ne 'VxKexNightlyLoaderCommand1' -or $saved.Sid -cne $sid -or $saved.CommandPath -cne $commandPath -or $saved.OwnedCommand -cne $ownedCommand) { throw 'Backup identity/command mismatch.' }
        if (@($saved.Keys).Count -ne $paths.Count) { throw 'Backup key count mismatch.' }
        for ($i=0;$i -lt $paths.Count;$i++) { if ($saved.Keys[$i].Path -cne $paths[$i]) { throw 'Backup key path mismatch.' } }
        $current = CurrentDefault
        if (-not $current.Exists -or $current.Kind -ne 'String' -or [string]$current.Data -cne $ownedCommand) { throw 'Current command is no longer exactly the owned value; restore refused.' }
        $prior = @($saved.Keys[3].Values | Where-Object { $_.Name -ceq '' })
        if ($prior.Count -gt 1) { throw 'Invalid duplicate default in backup.' }
        if ($prior.Count -eq 1 -and [string]$prior[0].Data -cne $expectedMachine -and [string]$prior[0].Data -cne $ownedCommand) { throw 'Saved default is outside the supported original/owned commands.' }
        if ($prior.Count -eq 1 -and ($prior[0].Kind -ne 'String' -and $prior[0].Kind -ne 'ExpandString')) { throw 'Unexpected saved default type.' }
        $key = $hcu.OpenSubKey($commandPath,$true)
        try {
            if ($prior.Count -eq 1) { $kind = [Enum]::Parse([Microsoft.Win32.RegistryValueKind],[string]$prior[0].Kind); $key.SetValue('', $prior[0].Data, $kind) }
            else { $key.DeleteValue('',$false) }
        } finally { $key.Close() }
        # Remove only keys this apply created, and only while still completely empty.
        for ($i=$paths.Count-1;$i -ge 0;$i--) {
            if (-not $saved.Keys[$i].Exists) {
                $key = $hcu.OpenSubKey($paths[$i],$false); $empty = $false
                try { if ($null -ne $key) { $empty = ($key.ValueCount -eq 0 -and $key.SubKeyCount -eq 0) } }
                finally { if ($null -ne $key) { $key.Close() } }
                if ($empty) { $hcu.DeleteSubKey($paths[$i],$false) }
            }
        }
        $after = CurrentDefault
        if ($prior.Count -eq 1) {
            if (-not $after.Exists -or $after.Kind -cne $prior[0].Kind -or [string]$after.Data -cne [string]$prior[0].Data) { throw 'Restored value readback mismatch.' }
        } elseif ($after.Exists) { throw 'Restored value should be absent.' }
        Say 'RESTORED: only the saved HKCU command default; newly created empty keys removed.'
        exit 0
    }
    InitializeInstalledPaths
    if (-not [IO.File]::Exists($nightly) -or -not [IO.File]::Exists($loader)) { throw 'Installed Nightly or native loader is missing.' }
    if ((HashFile $loader) -cne $loaderSha) { throw 'Installed native loader SHA-256 differs from the tested pin.' }
    $machine = $hlm.OpenSubKey($commandPath,$false)
    try {
        $registered = ValueRecord $machine ''
        if (-not $registered.Exists -or $registered.Kind -ne 'String' -or [string]$registered.Data -cne $expectedMachine) { throw 'Machine Nightly command differs from the verified exact original; no change made.' }
    } finally { if ($null -ne $machine) { $machine.Close() } }
    NoDelegateExecute
    CheckNightlyIfeo
    $before = CurrentDefault
    if ($before.Exists -and [string]$before.Data -cne $expectedMachine -and [string]$before.Data -cne $ownedCommand) { throw 'An unrelated HKCU command already exists; no change made.' }
    if ($before.Exists -and $before.Kind -ne 'String' -and $before.Kind -ne 'ExpandString') { throw 'Unexpected HKCU default value type.' }
    Say ('OriginalMachineCommand=' + [string]$registered.Data)
    Say ('PlannedUserCommand=' + $ownedCommand)
    Say ('HKCU.DefaultPresent=' + $before.Exists)
    if ($Mode -eq 'Plan') { Say 'PLAN ONLY: no registry or association changes.'; exit 0 }
    if ($before.Exists -and $before.Kind -eq 'String' -and [string]$before.Data -ceq $ownedCommand) { Say 'ALREADY APPLIED: no write and no new backup.'; exit 0 }
    $snapshots = @(); foreach ($path in $paths) { $snapshots += (Snapshot $path) }
    $backup = New-Object PSObject -Property @{ Schema='VxKexNightlyLoaderCommand1'; Sid=$sid;
        CommandPath=$commandPath; MachineCommand=$registered.Data; OwnedCommand=$ownedCommand;
        LoaderSha256=$loaderSha; LoaderPath=$loader; NightlyPath=$nightly; CreatedUtc=[DateTime]::UtcNow.ToString('o'); Keys=$snapshots }
    $writtenBackup = WriteNewBackup $backup $BackupPath
    Say ('Backup=' + $writtenBackup)
    # Recheck immediately before the only application write.
    NoDelegateExecute
    $now = CurrentDefault
    if ($now.Exists -ne $before.Exists -or $now.Kind -cne $before.Kind -or [string]$now.Data -cne [string]$before.Data) { throw 'HKCU command changed after backup; no write made.' }
    $key = $hcu.CreateSubKey($commandPath)
    try { $key.SetValue('',$ownedCommand,[Microsoft.Win32.RegistryValueKind]::String) }
    finally { $key.Close() }
    $after = CurrentDefault
    if (-not $after.Exists -or $after.Kind -ne 'String' -or [string]$after.Data -cne $ownedCommand) { throw 'Applied command readback mismatch; preserve backup and inspect.' }
    foreach ($old in $snapshots) {
        if ($old.Exists) { $new = Snapshot $old.Path; if ($new.Sddl -cne $old.Sddl) { throw ('ACL changed at ' + $old.Path + '; inspect with the preserved backup.') } }
    }
    Say ('APPLIED: HKCU\' + $commandPath + ' default REG_SZ=' + [string]$after.Data)
    Say 'Machine keys, UserChoice and IFEO were not modified. Browser execution was not attempted.'
    exit 0
} catch {
    Say ('ERROR: ' + $_.Exception.Message)
    exit 2
}
