$ErrorActionPreference = 'Stop'
$testRoot = $PSScriptRoot
$setEnv = 'C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd'
if (-not (Test-Path $setEnv)) {
    throw "Windows 7.1 SDK environment script not found: $setEnv"
}
foreach ($architecture in @('x86', 'x64')) {
    $temporaryDirectory = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [System.IO.Path]::GetTempPath() }
    $runner = Join-Path $temporaryDirectory ("vxkex-contracts-$architecture.cmd")
    @"
@echo off
call "$setEnv" /$architecture
if errorlevel 1 exit /b 1
python "$testRoot\contracts\run-tests.py" --compiler cl
if errorlevel 1 exit /b 1
python "$testRoot\chrome\run-tests.py" --compiler cl
if errorlevel 1 exit /b 1
python "$testRoot\propagation\run-tests.py" --compiler cl
if errorlevel 1 exit /b 1
python "$testRoot\winhttp\run-tests.py" --compiler cl
if errorlevel 1 exit /b 1
python "$testRoot\updater-com\run-tests.py" --compiler cl
if errorlevel 1 exit /b 1
python "$testRoot\services\run-tests.py" --compiler cl
if errorlevel 1 exit /b 1
python "$testRoot\uac\run-tests.py" --compiler cl
if errorlevel 1 exit /b 1
python "$testRoot\loader-elevation\run-tests.py" --compiler cl
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $runner -Encoding ASCII
    try {
        & cmd.exe /d /c $runner
        if ($LASTEXITCODE -ne 0) {
            throw "Windows SDK $architecture contract tests failed ($LASTEXITCODE)"
        }
    } finally {
        Remove-Item -LiteralPath $runner -Force -ErrorAction SilentlyContinue
    }
}
