<#
.SYNOPSIS
Collects a read-only comparison of Windows registry enumeration APIs.
.DESCRIPTION
Target syntax/runtime: Windows PowerShell 2.0 and .NET Framework 3.5.
The minimum runtime requires a separate Windows 7 validation; a newer PowerShell
CI run does not establish that compatibility. No server is started, no COM object
is activated, and no registry value is written or deleted. Only the report file
is written. This script diagnoses failures; it does not repair error 317.
.PARAMETER OutputPath
Local output file; defaults to VxKex-GoogleUpdater-Diagnostic.txt on the desktop.
.PARAMETER RegistryPath
Relative HKCU path. Only the known GoogleUpdater LocalServer32 key or the isolated
Software\VxKexNEXTDiagnosticsTest<32 hex digits>\LocalServer32 CI path is accepted.
.EXAMPLE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Inspect_GoogleUpdater.ps1
.EXAMPLE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Inspect_GoogleUpdater.ps1 -OutputPath C:\Temp\updater-diagnostic.txt
#>
[CmdletBinding()]
param(
    [string] $OutputPath = '',
    [string] $RegistryPath = 'Software\Classes\CLSID\{6E1E5C99-BC66-5838-926C-B47FC5A1B44A}\LocalServer32'
)
$ErrorActionPreference = 'Stop'
$knownPath = 'Software\Classes\CLSID\{6E1E5C99-BC66-5838-926C-B47FC5A1B44A}\LocalServer32'
if (($RegistryPath -ine $knownPath) -and
    ($RegistryPath -notmatch '^Software\\VxKexNEXTDiagnosticsTest[0-9A-Fa-f]{32}\\LocalServer32$')) {
    throw 'RegistryPath must be the known HKCU COM key or the isolated CI test key.'
}
if ([string]::IsNullOrEmpty($OutputPath)) {
    $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
    if ([string]::IsNullOrEmpty($desktop)) { throw 'The desktop path is unavailable; specify -OutputPath.' }
    $OutputPath = [IO.Path]::Combine($desktop, 'VxKex-GoogleUpdater-Diagnostic.txt')
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if ($OutputPath -notmatch '^[A-Za-z]:\\' -or $OutputPath.StartsWith('\\')) {
    throw 'OutputPath must be a local drive path; UNC and device paths are not supported.'
}
$drive = New-Object System.IO.DriveInfo -ArgumentList ([IO.Path]::GetPathRoot($OutputPath))
if ($drive.DriveType -eq [IO.DriveType]::Network) { throw 'Network report destinations are not supported.' }
if (-not [IO.Directory]::Exists([IO.Path]::GetDirectoryName($OutputPath))) {
    throw 'The report directory must already exist.'
}

$source = @'
using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace VxKexUpdaterDiagnostics {
    public static class NativeProbe {
        const uint View32 = 0x0200, View64 = 0x0100;
        const uint KeyRead = 0x20019;
        const int MaxBytes = 65536, MaxNameChars = 1024, MaxValues = 8;
        const int MaxReportChars = 49152, MaxCommandChars = 2048;
        static readonly IntPtr HKCU = new IntPtr(unchecked((int)0x80000001));
        static readonly UnicodeEncoding Unicode = new UnicodeEncoding(false, false, true);

        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern IntPtr GetModuleHandleW(string name);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegOpenKeyExW(IntPtr parent, string path, uint options, uint access, out IntPtr key);
        [DllImport("advapi32.dll", ExactSpelling=true)]
        static extern int RegCloseKey(IntPtr key);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegQueryInfoKeyW(IntPtr key, IntPtr cls, IntPtr clsLen, IntPtr reserved,
            out uint subkeys, out uint maxSubkey, IntPtr maxClass, out uint values,
            out uint maxValueName, out uint maxValueData, IntPtr security, IntPtr lastWrite);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegEnumValueW(IntPtr key, uint index, [Out] char[] name, ref uint nameChars,
            IntPtr reserved, out uint type, [Out] byte[] data, ref uint dataBytes);
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern int RegQueryValueExW(IntPtr key, string name, IntPtr reserved, out uint type,
            [Out] byte[] data, ref uint dataBytes);
        [DllImport("ntdll.dll", ExactSpelling=true)]
        static extern int NtEnumerateValueKey(IntPtr key, uint index, int informationClass,
            [Out] byte[] information, uint length, out uint resultLength);
        [DllImport("ntdll.dll", ExactSpelling=true)]
        static extern uint RtlNtStatusToDosError(int status);

        sealed class Value {
            internal bool Valid;
            internal string Name;
            internal uint Type;
            internal byte[] Data;
        }
        sealed class Report {
            internal StringBuilder Text = new StringBuilder();
            internal bool Truncated;
            internal void Line(string value) {
                if (Text.Length + value.Length + 2 <= MaxReportChars - 128)
                    Text.Append(value).Append("\r\n");
                else Truncated = true;
            }
            internal void Field(string key, object value) {
                Line(key + "=" + Convert.ToString(value, CultureInfo.InvariantCulture));
            }
            internal string Finish() {
                // Only these fixed markers use the tail reserved by Line().
                Text.Append("REPORT_TRUNCATED=").Append(Truncated ? "TRUE" : "FALSE").Append("\r\n");
                Text.Append("COLLECTION_COMPLETE=TRUE\r\n");
                if (Text.Length > MaxReportChars) throw new InvalidOperationException("Report limit exceeded");
                return Text.ToString();
            }
        }
        static string Escape(string text, int limit) {
            if (text == null) return "<unavailable>";
            StringBuilder result = new StringBuilder();
            int count = Math.Min(text.Length, limit);
            for (int i = 0; i < count; ++i) {
                char ch = text[i];
                if (ch == '\\') result.Append("\\\\");
                else if (ch < 32 || ch == 127) result.Append("\\u").Append(((int)ch).ToString("X4", CultureInfo.InvariantCulture));
                else result.Append(ch);
            }
            if (text.Length > limit) result.Append("<TRUNCATED>");
            return result.ToString();
        }
        static string Hash(byte[] bytes) {
            using (SHA256 algorithm = SHA256.Create()) {
                return BitConverter.ToString(algorithm.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
            }
        }
        static byte[] Slice(byte[] data, int start, int length) {
            byte[] result = new byte[length];
            Buffer.BlockCopy(data, start, result, 0, length);
            return result;
        }
        static bool Same(Value first, Value second) {
            if (!first.Valid || !second.Valid || first.Data == null || second.Data == null ||
                first.Name != second.Name || first.Type != second.Type || first.Data.Length != second.Data.Length) return false;
            for (int i = 0; i < first.Data.Length; ++i) if (first.Data[i] != second.Data[i]) return false;
            return true;
        }
        static string NameText(string name) { return name == "" ? "<default>" : Escape(name, 128); }
        static void ShowValue(Report report, string prefix, Value value) {
            report.Field(prefix + "_VALID", value.Valid ? "TRUE" : "FALSE");
            if (!value.Valid) return;
            report.Field(prefix + "_NAME", NameText(value.Name));
            report.Field(prefix + "_TYPE", value.Type);
            if (value.Data != null) {
                report.Field(prefix + "_DATA_CB", value.Data.Length);
                report.Field(prefix + "_DATA_SHA256", Hash(value.Data));
            }
        }
        static Value Enumerate(Report report, IntPtr key, uint index, uint maxSubkey, uint maxName, uint maxData) {
            uint largestName = Math.Max(maxSubkey, maxName);
            uint nameIn = largestName < MaxNameChars ? largestName + 1 : MaxNameChars;
            uint dataIn = maxData == 0 ? 1 : Math.Min(maxData, (uint)MaxBytes);
            char[] name = new char[(int)nameIn];
            byte[] data = new byte[(int)dataIn];
            uint nameOut = nameIn, dataOut = dataIn, type;
            int status = RegEnumValueW(key, index, name, ref nameOut, IntPtr.Zero, out type, data, ref dataOut);
            report.Field("REG_ENUM_STATUS", status);
            report.Field("REG_ENUM_NAME_IN_CCH", nameIn);
            report.Field("REG_ENUM_NAME_OUT_CCH", nameOut);
            report.Field("REG_ENUM_DATA_IN_CB", dataIn);
            report.Field("REG_ENUM_DATA_OUT_CB", dataOut);
            report.Field("REG_ENUM_TYPE_RAW", type);
            Value value = new Value();
            if (status == 0 && nameOut < nameIn && dataOut <= dataIn && name[(int)nameOut] == '\0') {
                string text = new string(name, 0, (int)nameOut);
                if (text.IndexOf('\0') < 0) {
                    value.Valid = true; value.Name = text; value.Type = type; value.Data = Slice(data, 0, (int)dataOut);
                }
            }
            ShowValue(report, "REG_ENUM", value);
            if (status == 234 && dataOut > MaxBytes) report.Field("REG_ENUM_LIMIT", "VALUE_TOO_LARGE");
            return value;
        }
        static Value EnumerateNative(Report report, IntPtr key, uint index, bool full) {
            string prefix = full ? "NT_FULL" : "NT_BASIC";
            byte[] buffer = new byte[MaxBytes];
            uint required;
            int status = NtEnumerateValueKey(key, index, full ? 1 : 0, buffer, (uint)buffer.Length, out required);
            report.Field(prefix + "_STATUS", "0x" + unchecked((uint)status).ToString("X8", CultureInfo.InvariantCulture));
            report.Field(prefix + "_DOS_STATUS", RtlNtStatusToDosError(status));
            report.Field(prefix + "_BUFFER_CB", buffer.Length);
            report.Field(prefix + "_RESULT_CB", required);
            Value value = new Value();
            int header = full ? 20 : 12;
            if (status == 0 && required >= header && required <= buffer.Length) {
                uint nameBytes = BitConverter.ToUInt32(buffer, full ? 16 : 8);
                report.Field(prefix + "_NAME_CB", nameBytes);
                if ((nameBytes & 1) == 0 && nameBytes / 2 < MaxNameChars && nameBytes <= required - header) {
                    string name = null;
                    try { name = Unicode.GetString(buffer, header, (int)nameBytes); }
                    catch (DecoderFallbackException) { report.Field(prefix + "_NAME_DECODE_STATUS", "INVALID_UTF16"); }
                    if (name != null && name.IndexOf('\0') < 0) {
                        value.Name = name; value.Type = BitConverter.ToUInt32(buffer, 4);
                        value.Valid = true;
                        if (full) {
                            uint offset = BitConverter.ToUInt32(buffer, 8), count = BitConverter.ToUInt32(buffer, 12);
                            report.Field(prefix + "_DATA_OFFSET", offset);
                            report.Field(prefix + "_DATA_CB_RAW", count);
                            // Empty registry data has no bytes to address. Native Windows
                            // may return a sentinel offset; do not dereference it.
                            if (count > MaxBytes || (count != 0 &&
                                (offset > required || count > required - offset ||
                                 offset < header + nameBytes))) value.Valid = false;
                            else value.Data = Slice(buffer, count == 0 ? 0 : (int)offset, (int)count);
                        }
                    }
                }
            }
            ShowValue(report, prefix, value);
            if (status != 0 && required > MaxBytes) report.Field(prefix + "_LIMIT", "VALUE_TOO_LARGE");
            return value;
        }
        static Value Query(Report report, IntPtr key, string name) {
            Value value = new Value();
            uint type, count = 0;
            int status = RegQueryValueExW(key, name, IntPtr.Zero, out type, null, ref count);
            report.Field("REG_QUERY_SIZE_STATUS", status);
            report.Field("REG_QUERY_REQUIRED_CB", count);
            if (status != 0) { ShowValue(report, "REG_QUERY", value); return value; }
            if (count > MaxBytes) {
                report.Field("REG_QUERY_LIMIT", "VALUE_TOO_LARGE");
                ShowValue(report, "REG_QUERY", value); return value;
            }
            byte[] data = new byte[count == 0 ? 1 : (int)count];
            uint capacity = count;
            status = RegQueryValueExW(key, name, IntPtr.Zero, out type, data, ref count);
            report.Field("REG_QUERY_STATUS", status);
            report.Field("REG_QUERY_DATA_IN_CB", capacity);
            report.Field("REG_QUERY_DATA_OUT_CB", count);
            if (status == 0 && count <= capacity) {
                value.Valid = true; value.Name = name; value.Type = type; value.Data = Slice(data, 0, (int)count);
            }
            ShowValue(report, "REG_QUERY", value);
            return value;
        }
        static void Compare(Report report, string name, Value first, Value second) {
            report.Field(name, first.Valid && second.Valid && first.Data != null && second.Data != null ?
                (Same(first, second) ? "TRUE" : "FALSE") : "UNAVAILABLE");
        }
        static void DefaultCommand(Report report, Value value) {
            if (!value.Valid || value.Name != "" || value.Data == null) return;
            if (value.Type != 1 && value.Type != 2) {
                report.Field("DEFAULT_COM_COMMAND_SKIPPED", "NOT_A_STRING"); return;
            }
            if ((value.Data.Length & 1) != 0) {
                report.Field("DEFAULT_COM_COMMAND_SKIPPED", "ODD_UTF16_LENGTH"); return;
            }
            try {
                string command = Unicode.GetString(value.Data);
                if (command.Length != 0 && command[command.Length - 1] == '\0') command = command.Substring(0, command.Length - 1);
                report.Field("DEFAULT_COM_COMMAND", Escape(command, MaxCommandChars));
                report.Field("DEFAULT_COM_COMMAND_TRUNCATED", command.Length > MaxCommandChars ? "TRUE" : "FALSE");
                report.Field("DEFAULT_COM_COMMAND_EXPANDED", "FALSE");
            } catch (DecoderFallbackException) {
                report.Field("DEFAULT_COM_COMMAND_SKIPPED", "INVALID_UTF16");
            }
        }
        static void InspectView(Report report, string path, uint view, string label) {
            report.Field("BEGIN_VIEW", label);
            IntPtr key;
            int status = RegOpenKeyExW(HKCU, path, 0, KeyRead | view, out key);
            report.Field("OPEN_STATUS", status);
            if (status != 0) { report.Field("END_VIEW", label); return; }
            try {
                uint subkeys, maxSubkey, values, maxName, maxData;
                status = RegQueryInfoKeyW(key, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero,
                    out subkeys, out maxSubkey, IntPtr.Zero, out values, out maxName, out maxData,
                    IntPtr.Zero, IntPtr.Zero);
                report.Field("QUERY_INFO_STATUS", status);
                if (status != 0) return;
                report.Field("QUERY_SUBKEYS", subkeys);
                report.Field("QUERY_MAX_SUBKEY_CCH", maxSubkey);
                report.Field("QUERY_VALUES", values);
                report.Field("QUERY_MAX_VALUE_NAME_CCH", maxName);
                report.Field("QUERY_MAX_VALUE_DATA_CB", maxData);
                uint limit = Math.Min(values, (uint)MaxValues);
                report.Field("ENUMERATION_TRUNCATED", values > limit ? "TRUE" : "FALSE");
                for (uint index = 0; index < limit; ++index) {
                    report.Field("BEGIN_VALUE", index);
                    Value enumerated = Enumerate(report, key, index, maxSubkey, maxName, maxData);
                    Value basic = EnumerateNative(report, key, index, false);
                    Value full = EnumerateNative(report, key, index, true);
                    string name = basic.Valid ? basic.Name : (full.Valid ? full.Name : (enumerated.Valid ? enumerated.Name : null));
                    report.Field("VALUE_NAME", NameText(name));
                    if (name != null) {
                        Value queried = Query(report, key, name);
                        Compare(report, "MATCH_ENUM_QUERY", enumerated, queried);
                        Compare(report, "MATCH_FULL_QUERY", full, queried);
                        if (basic.Valid) report.Field("MATCH_BASIC_QUERY_TYPE", queried.Valid ?
                            (basic.Type == queried.Type ? "TRUE" : "FALSE") : "UNAVAILABLE");
                        DefaultCommand(report, queried);
                    } else report.Field("REG_QUERY_SKIPPED", "NO_VALID_NAME");
                    report.Field("END_VALUE", index);
                }
            } finally {
                report.Field("CLOSE_STATUS", RegCloseKey(key));
                report.Field("END_VIEW", label);
            }
        }
        public static string Collect(string path) {
            Report report = new Report();
            report.Field("FORMAT", "VXKEX_GOOGLE_UPDATER_DIAGNOSTIC_1");
            report.Field("MODE", "READ_ONLY_DIAGNOSTIC");
            report.Field("REPAIRS_PERFORMED", "FALSE");
            report.Field("PROCESS_BITS", IntPtr.Size * 8);
            report.Field("KEXDLL_LOADED", GetModuleHandleW("KexDll.dll") == IntPtr.Zero ? "FALSE" : "TRUE");
            report.Field("HKCU_PATH", Escape(path, 1024));
            report.Field("LIMIT_VALUES_PER_VIEW", MaxValues);
            report.Field("LIMIT_BUFFER_CB", MaxBytes);
            report.Field("LIMIT_NAME_CCH", MaxNameChars);
            report.Line("NOTE=Separate API calls can observe concurrent registry changes; disagreement is diagnostic, not proof of corruption.");
            report.Line("NOTE=No registry writes, COM activation, server execution, uploads or executable-file inspection are performed.");
            InspectView(report, path, View32, "32");
            InspectView(report, path, View64, "64");
            return report.Finish();
        }
    }
}
'@
if ($null -eq ('VxKexUpdaterDiagnostics.NativeProbe' -as [type])) {
    Add-Type -TypeDefinition $source -Language CSharp
}
$report = [VxKexUpdaterDiagnostics.NativeProbe]::Collect($RegistryPath)
[IO.File]::WriteAllText($OutputPath, $report, [Text.Encoding]::UTF8)
Write-Output ('Diagnostic report: ' + $OutputPath)
