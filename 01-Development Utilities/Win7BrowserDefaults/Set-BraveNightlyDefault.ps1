param(
    [ValidateSet('Plan','Apply','Restore')][string]$Mode = 'Plan',
    [string]$BackupPath,
    [string]$ResultPath
)
$ErrorActionPreference = 'Stop'
try {
    $sourcePath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'NightlyDefaults.cs'
    if (-not ('VxKexNightlyDefaults.SetNightlyFour' -as [type])) {
        Add-Type -TypeDefinition ([IO.File]::ReadAllText($sourcePath)) -Language CSharp -ReferencedAssemblies 'System.dll','System.Xml.dll' -ErrorAction Stop
    }
    if (-not ('VxKexNightlyDefaults.SetNightlyFour' -as [type])) { throw 'Native association helper compilation did not produce its required type.' }
    $status = [VxKexNightlyDefaults.SetNightlyFour]::Run($BackupPath,$ResultPath,$Mode)
    exit $status
} catch {
    [Console]::Error.WriteLine($_.Exception.GetType().FullName + ': ' + $_.Exception.Message)
    exit 2
}
