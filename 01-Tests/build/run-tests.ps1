# Windows PowerShell 5.1. Exercises the real batch entrypoints in child cmd.exe
# processes. The fake tools only replace compiler/SDK/archive boundaries; assertions
# concern exit codes, executed stages, restored data, and produced files.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('VxKex build tests ' + [Guid]::NewGuid().ToString('N'))
$script:Assertions = 0
$script:FixtureNumber = 0
[IO.Directory]::CreateDirectory($testRoot) | Out-Null

function Write-Text($Path, $Text) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.Encoding]::ASCII)
}
function Assert-Equal($Expected, $Actual, $Message) {
    $script:Assertions++
    if ($Expected -cne $Actual) { throw "$Message (expected '$Expected', got '$Actual')" }
}
function Assert-True($Condition, $Message) {
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

$fakeTool = Join-Path $testRoot 'FakeTool.exe'
Add-Type -OutputAssembly $fakeTool -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
class FakeTool {
    static string Env(string name) { return Environment.GetEnvironmentVariable(name) ?? ""; }
    static int Code(int fallback) {
        int code;
        return Int32.TryParse(Env("VX_TEST_CODE"), out code) ? code : fallback;
    }
    static int Main(string[] args) {
        string tool = Path.GetFileNameWithoutExtension(Environment.GetCommandLineArgs()[0]);
        File.AppendAllText(Env("VX_TEST_CALLS"), tool + "|" + String.Join(" ", args) + "\r\n");
        if (String.Equals(tool, "MSBuild", StringComparison.OrdinalIgnoreCase)) {
            string platform = "", config = "";
            foreach (string arg in args) {
                if (arg.StartsWith("/p:Platform=", StringComparison.OrdinalIgnoreCase))
                    platform = arg.Substring("/p:Platform=".Length).Trim('"');
                if (arg.StartsWith("/p:Configuration=", StringComparison.OrdinalIgnoreCase))
                    config = arg.Substring("/p:Configuration=".Length).Trim('"');
            }
            if (platform == Env("VX_TEST_MSBUILD_FAIL")) return Code(21);
            if (platform == "Win32" && Env("VX_TEST_NO_INSTALLER") != "1")
                File.WriteAllText(Path.Combine(Env("VX_TEST_ROOT"), "KexSetup_" + config + ".exe"), "compiled installer");
        } else if (tool == "mlsbdic") {
            if (Env("VX_TEST_TOOL_FAIL") == "dictionary") return Code(45);
            if (Env("VX_TEST_NO_DICTIONARY") == "1") return 0;
            string output = Path.Combine(Environment.CurrentDirectory, "KexDir", "Globalization", "Dictionaries");
            Directory.CreateDirectory(output);
            foreach (string path in Directory.GetFiles("Dictionaries", "*.dic"))
                File.WriteAllText(Path.Combine(output, Path.GetFileNameWithoutExtension(path) + ".bdi"), "compiled dictionary");
        } else if (tool == "7zr") {
            string command = args.Length == 0 ? "" : args[0];
            if (command == "a") {
                if (Env("VX_TEST_TOOL_FAIL") == "archive") return Code(41);
                string[] files = Directory.GetFiles("Archive", "*", SearchOption.AllDirectories);
                Array.Sort(files, StringComparer.OrdinalIgnoreCase);
                File.WriteAllText("Archive.7z", "fake archive boundary\r\n" + String.Join("\r\n", files));
            }
            if (command == "t" && Env("VX_TEST_TOOL_FAIL") == "archive-test") return Code(42);
        } else if (tool == "vtrplnt" && Env("VX_TEST_TOOL_FAIL") == "version") {
            return Code(43);
        } else if (tool == "vautogen" && Env("VX_TEST_TOOL_FAIL") == "autogen") {
            return Code(44);
        }
        return 0;
    }
}
'@

function New-Fixture {
    $script:FixtureNumber++
    $fixture = Join-Path $testRoot ('project with spaces ' + $script:FixtureNumber)
    $utilities = Join-Path $fixture '01-Development Utilities'
    [IO.Directory]::CreateDirectory((Join-Path $utilities '7zSfx')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $fixture 'tools')) | Out-Null
    foreach ($name in @('Build_Debug.bat', 'Build_Release.bat', 'Build_Stable_Version_And_Clean.bat', 'Clean_VxKex_NEXT.bat')) {
        Copy-Item -LiteralPath (Join-Path (Join-Path $repoRoot '01-Development Utilities') $name) -Destination $utilities
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot '01-Development Utilities\7zSfx\makesfx.bat') -Destination (Join-Path $utilities '7zSfx')
    Copy-Item -LiteralPath $fakeTool -Destination (Join-Path $fixture 'tools\MSBuild.exe')
    Write-Text (Join-Path $fixture 'tools\SetEnv.cmd') @'
@echo off
if /I "%~1"=="%VX_TEST_SETENV_FAIL%" exit /b 31
exit /b 0
'@
    Write-Text (Join-Path $fixture '00-Common Headers\vautogen.h') 'header-before'
    Write-Text (Join-Path $utilities 'vautogen\vautogen.ini') 'ini-before'
    return $fixture
}

function Invoke-Batch($Fixture, $RelativePath, $Settings = @{}, $Arguments = '') {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $env:ComSpec
    $info.Arguments = '/d /s /c ""{0}" {1}"' -f (Join-Path $Fixture $RelativePath), $Arguments
    $info.WorkingDirectory = $Fixture
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $defaults = @{
        'PATH' = ((Join-Path $Fixture 'tools') + ';' + (Join-Path $env:SystemRoot 'System32'))
        'VxKex_NEXT_Platform_Toolset' = 'Windows7.1SDK'
        'VxKex_NEXT_NOPAUSE' = '1'
        'VX_TEST_ROOT' = $Fixture
        'VX_TEST_CALLS' = (Join-Path $Fixture 'calls.log')
        'VX_TEST_SETENV_FAIL' = ''
        'VX_TEST_MSBUILD_FAIL' = ''
        'VX_TEST_TOOL_FAIL' = ''
        'VX_TEST_NO_INSTALLER' = ''
        'VX_TEST_NO_DICTIONARY' = ''
        'VX_TEST_CODE' = ''
        'VX_TEST_DEBUG_EXIT' = '0'
        'VX_TEST_RELEASE_EXIT' = '0'
        'VX_TEST_CLEAN_EXIT' = '0'
    }
    foreach ($entry in $Settings.GetEnumerator()) { $defaults[$entry.Key] = [string] $entry.Value }
    foreach ($entry in $defaults.GetEnumerator()) { $info.EnvironmentVariables[$entry.Key] = [string] $entry.Value }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    [void] $process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw "Batch command timed out: $RelativePath"
    }
    $result = [PSCustomObject]@{
        ExitCode = $process.ExitCode
        Output = ($stdout.Result + $stderr.Result)
        Calls = ''
    }
    $process.Dispose()
    if (Test-Path -LiteralPath (Join-Path $Fixture 'calls.log')) {
        $result.Calls = [IO.File]::ReadAllText((Join-Path $Fixture 'calls.log'))
    }
    return $result
}

function New-PackageFixture {
    $fixture = New-Fixture
    $utilities = Join-Path $fixture '01-Development Utilities'
    foreach ($name in @('7zr', 'vtrplnt')) {
        Copy-Item -LiteralPath $fakeTool -Destination (Join-Path $utilities ('7zSfx\' + $name + '.exe'))
    }
    Copy-Item -LiteralPath $fakeTool -Destination (Join-Path $utilities 'vautogen\vautogen.exe')
    Write-Text (Join-Path $utilities '7zSfx\7zS2.sfx') 'sfx header'
    Write-Text (Join-Path $fixture '00-Documentation\Application Compatibility List.docx') 'test document'
    Write-Text (Join-Path $fixture '00-Documentation\Changelog.txt') 'test changelog'
    Write-Text (Join-Path $fixture '02-Prebuilt Data\Dictionaries\Example.dic') 'test dictionary source'
    Write-Text (Join-Path $fixture '02-Prebuilt Data\KexDir\Certificates\ROOT.sst') 'test certificate bundle'
    Copy-Item -LiteralPath $fakeTool -Destination (Join-Path $fixture '02-Prebuilt Data\mlsbdic.exe')
    $core = @('KexDll.dll', 'KexShlEx.dll', 'KexCfg.exe', 'VxlView.exe', 'CpiwBypa.dll', 'CpiwBypaLdr.exe', 'VxKexLdr.exe')
    $extensions = @('KxAdvapi', 'KxBase', 'KxCom', 'KxCrt', 'KxCryp', 'KxDw', 'KxDx', 'KxMi', 'KxNet', 'KxNt', 'KxSchanl', 'KxUia', 'KxUser')
    foreach ($config in @('Debug', 'Release')) {
        foreach ($prefix in @('', 'x64\')) {
            $output = Join-Path $fixture ($prefix + $config)
            foreach ($name in $core) {
                Write-Text (Join-Path $output $name) ('test binary ' + $name)
                if ($config -eq 'Debug') {
                    Write-Text (Join-Path $output ([IO.Path]::GetFileNameWithoutExtension($name) + '.pdb')) 'test symbols'
                }
            }
            foreach ($name in $extensions) {
                Write-Text (Join-Path $output ($name + '.dll')) ('test binary ' + $name)
                if ($config -eq 'Debug') { Write-Text (Join-Path $output ($name + '.pdb')) 'test symbols' }
            }
        }
        Write-Text (Join-Path $fixture ($config + '\KexSetup.exe')) 'test setup'
        if ($config -eq 'Debug') { Write-Text (Join-Path $fixture 'Debug\KexSetup.pdb') 'test setup symbols' }
    }
    foreach ($arch in @('x86', 'x64')) {
        foreach ($name in @('dwrw10.dll', 'icuuc.dll', 'msvcp_win.dll', 'ucrtbase.dll', 'custom.dll')) {
            Write-Text (Join-Path $fixture ('02-Prebuilt DLLs\' + $arch + '\' + $name)) 'test runtime'
        }
    }
    Write-Text (Join-Path $fixture '02-Prebuilt DLLs\x86\mshtmlmedia.dll') 'test runtime'
    Write-Text (Join-Path $fixture '02-Prebuilt DLLs\x64\mfdevmgr.dll') 'test runtime'
    return $fixture
}

function Assert-NoInstaller($Fixture, $Config) {
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $Fixture ('KexSetup_' + $Config + '.exe')))) 'Failed packaging left an installer'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $Fixture ('KexSetup_' + $Config + '.pending')))) 'Failed packaging left a pending installer'
}

try {
    foreach ($config in @('Debug', 'Release')) {
        $scriptPath = '01-Development Utilities\Build_' + $config + '.bat'
        foreach ($failure in @('/x64', '/x86')) {
            $fixture = New-Fixture
            $result = Invoke-Batch $fixture $scriptPath @{ VX_TEST_SETENV_FAIL = $failure }
            Assert-Equal 31 $result.ExitCode ("$config SetEnv $failure error was lost: " + $result.Output)
            $expectedBuilds = 0
            if ($failure -eq '/x86') { $expectedBuilds = 1 }
            Assert-Equal $expectedBuilds ([regex]::Matches($result.Calls, 'MSBuild\|').Count) 'Build continued after failed SetEnv'
            Assert-NoInstaller $fixture $config
        }
        foreach ($platform in @('x64', 'Win32')) {
            $fixture = New-Fixture
            $result = Invoke-Batch $fixture $scriptPath @{ VX_TEST_MSBUILD_FAIL = $platform; VX_TEST_CODE = '23' }
            Assert-Equal 23 $result.ExitCode ("$config MSBuild $platform error was lost: " + $result.Output)
            $expectedBuilds = 1
            if ($platform -eq 'Win32') { $expectedBuilds = 2 }
            Assert-Equal $expectedBuilds ([regex]::Matches($result.Calls, 'MSBuild\|').Count) 'Build continued after failed compiler'
            Assert-NoInstaller $fixture $config
        }
        $fixture = New-Fixture
        $result = Invoke-Batch $fixture $scriptPath @{ VX_TEST_MSBUILD_FAIL = 'x64'; VX_TEST_CODE = '-7' }
        Assert-Equal -7 $result.ExitCode 'Negative compiler exit status was ignored'
        $fixture = New-Fixture
        Remove-Item -LiteralPath (Join-Path $fixture 'tools\MSBuild.exe')
        Write-Text (Join-Path $fixture ('KexSetup_' + $config + '.exe')) 'stale installer'
        $result = Invoke-Batch $fixture $scriptPath
        Assert-True ($result.ExitCode -ne 0) 'Missing MSBuild must fail'
        Assert-NoInstaller $fixture $config
        $fixture = New-Fixture
        $result = Invoke-Batch $fixture $scriptPath @{ VX_TEST_NO_INSTALLER = '1' }
        Assert-True ($result.ExitCode -ne 0) 'Successful compiler without installer must fail'
        $fixture = New-Fixture
        $result = Invoke-Batch $fixture $scriptPath @{ VxKex_NEXT_Platform_Toolset = 'v140_xp' }
        Assert-Equal 0 $result.ExitCode ("Complete $config build failed: " + $result.Output)
        Assert-Equal 2 ([regex]::Matches($result.Calls, 'MSBuild\|').Count) 'Both architectures must build'
        Assert-Equal 2 ([regex]::Matches($result.Calls, '/p:PlatformToolset=v140_xp').Count) 'Selected toolset was not forwarded'
        Assert-Equal 2 ([regex]::Matches($result.Calls, ('/p:Configuration=' + $config)).Count) 'Selected configuration was not forwarded'
    }

    # The orchestration script must restore version data even if Debug fails.
    foreach ($failedStage in @('Debug', 'Release', 'Clean', 'None')) {
        $fixture = New-Fixture
        $utilities = Join-Path $fixture '01-Development Utilities'
        Write-Text (Join-Path $utilities 'Build_Debug.bat') @'
@echo off
>>"%VX_TEST_CALLS%" echo Debug
>"%~dp0..\00-Common Headers\vautogen.h" echo changed header
>"%~dp0vautogen\vautogen.ini" echo changed ini
exit /b %VX_TEST_DEBUG_EXIT%
'@
        Write-Text (Join-Path $utilities 'Build_Release.bat') "@echo off`r`n>>`"%VX_TEST_CALLS%`" echo Release`r`nexit /b %VX_TEST_RELEASE_EXIT%`r`n"
        Write-Text (Join-Path $utilities 'Clean_VxKex_NEXT.bat') "@echo off`r`n>>`"%VX_TEST_CALLS%`" echo Clean`r`nexit /b %VX_TEST_CLEAN_EXIT%`r`n"
        $settings = @{}
        $expectedCode = 0
        $expectedCalls = "Debug`r`nRelease`r`nClean`r`n"
        if ($failedStage -ne 'None') { $expectedCode = 27 }
        if ($failedStage -eq 'Debug') { $settings.VX_TEST_DEBUG_EXIT = '27'; $expectedCalls = "Debug`r`n" }
        if ($failedStage -eq 'Release') { $settings.VX_TEST_RELEASE_EXIT = '27'; $expectedCalls = "Debug`r`nRelease`r`n" }
        if ($failedStage -eq 'Clean') { $settings.VX_TEST_CLEAN_EXIT = '27' }
        $result = Invoke-Batch $fixture '01-Development Utilities\Build_Stable_Version_And_Clean.bat' $settings
        Assert-Equal $expectedCode $result.ExitCode ('Stable build exit status: ' + $result.Output)
        Assert-Equal $expectedCalls $result.Calls 'Stable build did not stop at failed stage'
        Assert-Equal 'header-before' ([IO.File]::ReadAllText((Join-Path $fixture '00-Common Headers\vautogen.h'))) 'Version header was not restored'
        Assert-Equal 'ini-before' ([IO.File]::ReadAllText((Join-Path $utilities 'vautogen\vautogen.ini'))) 'Version config was not restored'
    }

    foreach ($config in @('Release', 'Debug')) {
        $fixture = New-PackageFixture
        $result = Invoke-Batch $fixture '01-Development Utilities\7zSfx\makesfx.bat' @{} ('/' + $config)
        Assert-Equal 0 $result.ExitCode ('Complete package failed: ' + $result.Output)
        $installer = [IO.File]::ReadAllText((Join-Path $fixture ('KexSetup_' + $config + '.exe')))
        foreach ($expected in @('Core32\KexDll.dll', 'Core64\KexDll.dll', 'Kex32\KxBase.dll', 'Kex64\KxBase.dll', 'Kex32\custom.dll', 'Kex64\custom.dll', 'Example.bdi')) {
            Assert-True ($installer.Contains($expected)) ('Installer did not contain ' + $expected)
        }
        Assert-True ($result.Calls.Contains('7zr|t Archive.7z')) 'Archive integrity command was not executed'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture '01-Development Utilities\7zSfx\Archive'))) 'Package staging directory was not removed'
    }
    foreach ($missing in @('x64\Release\KexDll.dll', '02-Prebuilt DLLs\x86\ucrtbase.dll', '00-Documentation\Changelog.txt', '02-Prebuilt Data\KexDir\Certificates\ROOT.sst')) {
        $fixture = New-PackageFixture
        Remove-Item -LiteralPath (Join-Path $fixture $missing)
        Write-Text (Join-Path $fixture 'KexSetup_Release.exe') 'stale installer'
        $result = Invoke-Batch $fixture '01-Development Utilities\7zSfx\makesfx.bat' @{} '/Release'
        Assert-True ($result.ExitCode -ne 0) ('Package succeeded without ' + $missing)
        Assert-NoInstaller $fixture 'Release'
        Assert-True (-not $result.Calls.Contains('7zr|a ')) 'Incomplete package reached the archiver'
    }
    $fixture = New-PackageFixture
    Write-Text (Join-Path $fixture 'x64\Release\KxBase.dll') ''
    $result = Invoke-Batch $fixture '01-Development Utilities\7zSfx\makesfx.bat' @{} '/Release'
    Assert-True ($result.ExitCode -ne 0) 'Package accepted an empty binary'
    Assert-NoInstaller $fixture 'Release'
    $fixture = New-PackageFixture
    Remove-Item -LiteralPath (Join-Path $fixture 'x64\Debug\KexDll.pdb')
    $result = Invoke-Batch $fixture '01-Development Utilities\7zSfx\makesfx.bat' @{} '/Debug'
    Assert-True ($result.ExitCode -ne 0) 'Debug package accepted missing symbols'
    Assert-NoInstaller $fixture 'Debug'
    foreach ($failure in @('dictionary', 'archive', 'archive-test', 'version', 'autogen')) {
        $fixture = New-PackageFixture
        $result = Invoke-Batch $fixture '01-Development Utilities\7zSfx\makesfx.bat' @{ VX_TEST_TOOL_FAIL = $failure; VX_TEST_CODE = '47' } '/Debug'
        Assert-Equal 47 $result.ExitCode ("$failure error was lost: " + $result.Output)
        Assert-NoInstaller $fixture 'Debug'
    }
    $fixture = New-PackageFixture
    $result = Invoke-Batch $fixture '01-Development Utilities\7zSfx\makesfx.bat' @{ VX_TEST_NO_DICTIONARY = '1' } '/Release'
    Assert-True ($result.ExitCode -ne 0) 'Dictionary compiler success without output must fail'
    Assert-NoInstaller $fixture 'Release'
    $fixture = New-PackageFixture
    $lock = [IO.File]::Open((Join-Path $fixture 'x64\Release\KxBase.dll'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        $result = Invoke-Batch $fixture '01-Development Utilities\7zSfx\makesfx.bat' @{} '/Release'
        Assert-True ($result.ExitCode -ne 0) 'Package ignored a failed mandatory copy'
        Assert-NoInstaller $fixture 'Release'
    } finally { $lock.Dispose() }

    $fixture = New-Fixture
    Write-Text (Join-Path $fixture '02-Prebuilt DLLs\x64\keep.dll') 'prebuilt'
    Write-Text (Join-Path $fixture 'x64\Debug\remove.dll') 'build output'
    $result = Invoke-Batch $fixture '01-Development Utilities\Clean_VxKex_NEXT.bat'
    Assert-Equal 0 $result.ExitCode ('Cleanup failed: ' + $result.Output)
    Assert-True (Test-Path -LiteralPath (Join-Path $fixture '02-Prebuilt DLLs\x64\keep.dll')) 'Cleanup deleted prebuilt x64 runtime'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture 'x64'))) 'Cleanup retained build outputs'
    $fixture = New-Fixture
    Write-Text (Join-Path $fixture 'x64\Debug\locked.dll') 'locked output'
    $lock = [IO.File]::Open((Join-Path $fixture 'x64\Debug\locked.dll'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        $result = Invoke-Batch $fixture '01-Development Utilities\Clean_VxKex_NEXT.bat'
        Assert-True ($result.ExitCode -ne 0) 'Cleanup hid an undeletable build file'
    } finally { $lock.Dispose() }
    Write-Host ("PASS: {0} build/packaging assertions in real cmd.exe subprocesses." -f $script:Assertions)
} finally {
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}
