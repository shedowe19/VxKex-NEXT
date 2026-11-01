# Invoke separately with the x86 and x64 Windows PowerShell executables.
# The child uses the documented -File entry point in the SAME architecture.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent (Split-Path -Parent $here)
$probe = Join-Path $root '01-Development Utilities\Inspect_GoogleUpdater.ps1'
$engine = Join-Path $PSHOME 'powershell.exe'
$testRootName = 'Software\VxKexNEXTDiagnosticsTest' + [Guid]::NewGuid().ToString('N')
$registryPath = $testRootName + '\LocalServer32'
$tempPath = Join-Path ([IO.Path]::GetTempPath()) ('VxKexNEXTDiagnosticsTest' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($tempPath) | Out-Null

$testSource = @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
namespace VxKexUpdaterDiagnosticsTest {
    public static class Fixture {
        static readonly IntPtr HKCU = new IntPtr(unchecked((int)0x80000001));
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegCreateKeyExW(IntPtr parent, string path, uint reserved, string cls,
            uint options, uint access, IntPtr security, out IntPtr key, out uint disposition);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegOpenKeyExW(IntPtr parent, string path, uint options, uint access, out IntPtr key);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegSetValueExW(IntPtr key, string name, uint reserved, uint type, byte[] data, uint size);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegDeleteValueW(IntPtr key, string name);
        [DllImport("advapi32.dll", ExactSpelling=true)]
        static extern int RegCloseKey(IntPtr key);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegQueryInfoKeyW(IntPtr key, IntPtr cls, IntPtr clsLength, IntPtr reserved,
            out uint subkeys, out uint maxSubkey, IntPtr maxClass, out uint values, out uint maxName,
            out uint maxData, IntPtr security, out long lastWrite);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegEnumValueW(IntPtr key, uint index, [Out] char[] name, ref uint chars,
            IntPtr reserved, out uint type, [Out] byte[] data, ref uint bytes);
        static void Check(int status) { if (status != 0) throw new InvalidOperationException("Fixture registry error " + status); }
        static string Hash(byte[] bytes) {
            using (SHA256 hash = SHA256.Create()) {
                return BitConverter.ToString(hash.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
            }
        }
        public static string DefaultValue() {
            return "\"C:\\NotExecuted\\updater.exe\" --server " + new string('x', 4096);
        }
        public static string DefaultHash() { return Hash(Encoding.Unicode.GetBytes(DefaultValue() + "\0")); }
        public static string BinaryHash() { return Hash(new byte[] { 0, 1, 2, 127, 128, 254, 255, 0 }); }
        public static string EmptyHash() { return Hash(new byte[0]); }
        public static void Create(string path, uint view) {
            IntPtr key; uint disposition;
            Check(RegCreateKeyExW(HKCU, path, 0, null, 0, 0x2001f | view, IntPtr.Zero, out key, out disposition));
            try {
                byte[] text = Encoding.Unicode.GetBytes(DefaultValue() + "\0");
                Check(RegSetValueExW(key, "", 0, 1, text, (uint)text.Length));
                byte[] binary = new byte[] { 0, 1, 2, 127, 128, 254, 255, 0 };
                Check(RegSetValueExW(key, "BinaryValue", 0, 3, binary, (uint)binary.Length));
                Check(RegSetValueExW(key, "EmptyBinary", 0, 3, new byte[0], 0));
                byte[] large = new byte[65537]; large[0] = 42; large[large.Length-1] = 43;
                Check(RegSetValueExW(key, "TooLarge", 0, 3, large, (uint)large.Length));
            } finally { Check(RegCloseKey(key)); }
        }
        public static string Snapshot(string path, uint view) {
            IntPtr key;
            Check(RegOpenKeyExW(HKCU, path, 0, 0x20019 | view, out key));
            try {
                uint subkeys, maxSubkey, values, maxName, maxData; long lastWrite;
                Check(RegQueryInfoKeyW(key, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero,
                    out subkeys, out maxSubkey, IntPtr.Zero, out values, out maxName, out maxData,
                    IntPtr.Zero, out lastWrite));
                if (values > 8 || maxName > 1024 || maxData > 131072) throw new InvalidOperationException("Fixture exceeded limits");
                List<string> entries = new List<string>();
                entries.Add("TIME=" + lastWrite.ToString(CultureInfo.InvariantCulture));
                entries.Add("SUBKEYS=" + subkeys.ToString(CultureInfo.InvariantCulture));
                for (uint i = 0; i < values; ++i) {
                    char[] name = new char[(int)maxName + 1]; byte[] data = new byte[maxData == 0 ? 1 : (int)maxData];
                    uint chars = (uint)name.Length, bytes = (uint)data.Length, type;
                    Check(RegEnumValueW(key, i, name, ref chars, IntPtr.Zero, out type, data, ref bytes));
                    byte[] actual = new byte[(int)bytes]; Buffer.BlockCopy(data, 0, actual, 0, actual.Length);
                    entries.Add(new string(name, 0, (int)chars) + "|" + type + "|" + bytes + "|" + Hash(actual));
                }
                entries.Sort(StringComparer.Ordinal);
                return String.Join("\n", entries.ToArray());
            } finally { Check(RegCloseKey(key)); }
        }
        public static void KeepOnlyDefault(string path, uint view) {
            IntPtr key;
            Check(RegOpenKeyExW(HKCU, path, 0, 0x0002 | view, out key));
            try {
                foreach (string name in new string[] { "BinaryValue", "EmptyBinary", "TooLarge" }) {
                    int status = RegDeleteValueW(key, name);
                    if (status != 2) Check(status);
                }
            } finally { Check(RegCloseKey(key)); }
        }
    }
}
'@
if ($null -eq ('VxKexUpdaterDiagnosticsTest.Fixture' -as [type])) {
    Add-Type -TypeDefinition $testSource -Language CSharp
}

function Assert-True($condition, [string] $message) {
    if (-not $condition) { throw $message }
}
function Invoke-Probe([string] $reportPath, [string] $keyPath) {
    # The random fixture paths cannot contain quotes; quote the filesystem paths.
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $engine
    $start.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $probe +
        '" -OutputPath "' + $reportPath + '" -RegistryPath "' + $keyPath + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        Assert-True ($process.Start()) 'Could not start diagnostic subprocess.'
        if (-not $process.WaitForExit(120000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Diagnostic subprocess timed out.'
        }
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        Assert-True ($process.ExitCode -eq 0) ('Diagnostic subprocess failed: ' + $stdout + $stderr)
        Assert-True ([IO.File]::Exists($reportPath)) 'Diagnostic report was not written.'
        return [IO.File]::ReadAllText($reportPath, [Text.Encoding]::UTF8)
    } finally { $process.Dispose() }
}

try {
    foreach ($view in @(512, 256)) { [VxKexUpdaterDiagnosticsTest.Fixture]::Create($registryPath, $view) }
    $before32 = [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 512)
    $before64 = [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 256)
    $report = Invoke-Probe (Join-Path $tempPath 'report.txt') $registryPath
    Assert-True ($before32 -ceq [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 512)) 'The 32-bit key changed, including its last-write timestamp.'
    Assert-True ($before64 -ceq [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 256)) 'The 64-bit key changed, including its last-write timestamp.'
    Assert-True ($report.Length -le 49152) 'The report exceeded its output limit.'
    Assert-True ($report.Contains('PROCESS_BITS=' + ([IntPtr]::Size * 8))) 'The child ran with unexpected process bitness.'
    Assert-True ($report.Contains('MODE=READ_ONLY_DIAGNOSTIC')) 'Missing read-only mode.'
    Assert-True ($report.Contains('REPAIRS_PERFORMED=FALSE')) 'The report must not claim a repair.'
    Assert-True ($report.Contains('REPORT_TRUNCATED=FALSE')) 'The bounded fixture report was unexpectedly truncated.'
    Assert-True ($report.Contains('COLLECTION_COMPLETE=TRUE')) 'Incomplete collection.'
    $views = [regex]::Matches($report, '(?ms)^BEGIN_VIEW=(32|64)\r?\n(.*?)^END_VIEW=\1\r?$')
    Assert-True ($views.Count -eq 2) 'Both registry views must be reported.'
    foreach ($viewBlock in $views) {
        $body = $viewBlock.Groups[2].Value
        Assert-True ($body.Contains('OPEN_STATUS=0')) 'The fixture key did not open.'
        Assert-True ($body.Contains('QUERY_INFO_STATUS=0')) 'RegQueryInfoKeyW failed.'
        Assert-True ($body.Contains('QUERY_VALUES=4')) 'Unexpected fixture value count.'
        $values = [regex]::Matches($body, '(?ms)^BEGIN_VALUE=\d+\r?\n(.*?)^END_VALUE=\d+\r?$')
        Assert-True ($values.Count -eq 4) 'Expected four independently reported values.'
        foreach ($valueBlock in $values) {
            $value = $valueBlock.Groups[1].Value
            Assert-True ($value.Contains('NT_BASIC_STATUS=0x00000000')) 'Basic native enumeration failed.'
            Assert-True ($value.Contains('NT_BASIC_VALID=TRUE')) 'Basic native layout was not validated.'
            if ($value.Contains('VALUE_NAME=TooLarge')) {
                Assert-True ($value.Contains('REG_ENUM_STATUS=234')) 'Oversized data must return ERROR_MORE_DATA.'
                Assert-True ($value.Contains('REG_ENUM_VALID=FALSE')) 'Oversized data was falsely marked valid.'
                Assert-True ($value.Contains('NT_FULL_VALID=FALSE')) 'Oversized FullInformation was falsely accepted.'
                Assert-True ($value.Contains('REG_QUERY_LIMIT=VALUE_TOO_LARGE')) 'Query did not enforce its allocation cap.'
                Assert-True ($value.Contains('MATCH_FULL_QUERY=UNAVAILABLE')) 'Missing data was falsely marked as matching.'
            } else {
                Assert-True ($value.Contains('REG_ENUM_STATUS=0')) 'RegEnumValueW failed for bounded fixture data.'
                Assert-True ($value.Contains('NT_FULL_STATUS=0x00000000')) 'Full native enumeration failed.'
                Assert-True ($value.Contains('NT_FULL_VALID=TRUE')) 'Full native layout was not validated.'
                Assert-True ($value.Contains('REG_QUERY_STATUS=0')) 'RegQueryValueExW failed for bounded fixture data.'
                Assert-True ($value.Contains('MATCH_ENUM_QUERY=TRUE')) 'Enumeration and direct-query data differ.'
                Assert-True ($value.Contains('MATCH_FULL_QUERY=TRUE')) 'Native FullInformation and direct-query data differ.'
                if ($value.Contains('VALUE_NAME=<default>')) {
                    Assert-True ($value.Contains('REG_QUERY_DATA_SHA256=' + [VxKexUpdaterDiagnosticsTest.Fixture]::DefaultHash())) 'Long default REG_SZ bytes differ.'
                    Assert-True ($value.Contains('DEFAULT_COM_COMMAND_TRUNCATED=TRUE')) 'Long default text must be explicitly truncated.'
                } elseif ($value.Contains('VALUE_NAME=BinaryValue')) {
                    Assert-True ($value.Contains('REG_QUERY_DATA_SHA256=' + [VxKexUpdaterDiagnosticsTest.Fixture]::BinaryHash())) 'Binary bytes differ.'
                    Assert-True (-not $value.Contains('DEFAULT_COM_COMMAND=')) 'Named binary data must not be printed as a command.'
                } elseif ($value.Contains('VALUE_NAME=EmptyBinary')) {
                    Assert-True ($value.Contains('REG_QUERY_DATA_CB=0')) 'The zero-length value was not preserved.'
                    Assert-True ($value.Contains('REG_QUERY_DATA_SHA256=' + [VxKexUpdaterDiagnosticsTest.Fixture]::EmptyHash())) 'Zero-length data hash differs.'
                } else { throw 'Unexpected fixture value in diagnostic report.' }
            }
        }
    }
    # Reproduce the caller's default-only key with exactly one WCHAR for its
    # empty value name, independently of the named-value fixture above.
    foreach ($view in @(512, 256)) { [VxKexUpdaterDiagnosticsTest.Fixture]::KeepOnlyDefault($registryPath, $view) }
    $before32 = [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 512)
    $before64 = [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 256)
    $single = Invoke-Probe (Join-Path $tempPath 'default-only.txt') $registryPath
    Assert-True ([regex]::Matches($single, '(?m)^REG_ENUM_NAME_IN_CCH=1\r?$').Count -eq 2) 'Default-only keys must use one WCHAR for the unnamed value.'
    Assert-True ([regex]::Matches($single, '(?m)^MATCH_ENUM_QUERY=TRUE\r?$').Count -eq 2) 'Default-only enumeration failed to preserve the value.'
    Assert-True ($before32 -ceq [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 512)) 'The default-only 32-bit key changed.'
    Assert-True ($before64 -ceq [VxKexUpdaterDiagnosticsTest.Fixture]::Snapshot($registryPath, 256)) 'The default-only 64-bit key changed.'
    # A missing key is a diagnostic failure, not a fabricated successful value.
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($testRootName)
    $missing = Invoke-Probe (Join-Path $tempPath 'missing.txt') $registryPath
    Assert-True ([regex]::Matches($missing, '(?m)^OPEN_STATUS=2\r?$').Count -eq 2) 'Missing views must report ERROR_FILE_NOT_FOUND.'
    Assert-True (-not $missing.Contains('BEGIN_VALUE=')) 'A missing key produced a fabricated value.'
    Assert-True (-not $missing.Contains('_VALID=TRUE')) 'A missing key was falsely reported as valid.'
    $unexpected = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testRootName)
    try { Assert-True ($null -eq $unexpected) 'The read-only probe recreated the missing key.' }
    finally { if ($null -ne $unexpected) { $unexpected.Close() } }
    Write-Output ('GoogleUpdater diagnostic subprocess/no-write tests passed (' + ([IntPtr]::Size * 8) + '-bit PowerShell).')
    Write-Output 'The CI runtime does not establish Windows PowerShell 2.0/.NET 3.5 compatibility.'
} finally {
    # This exact randomly generated HKCU subtree is the only registry tree owned
    # by this test. The production diagnostic script has no write/delete APIs.
    $owned = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testRootName)
    if ($null -ne $owned) {
        $owned.Close()
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($testRootName)
    }
    if ([IO.Directory]::Exists($tempPath)) { [IO.Directory]::Delete($tempPath, $true) }
}
