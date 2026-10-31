@echo off
setlocal EnableExtensions DisableDelayedExpansion

where MSBuild.exe >nul 2>nul
if not "%errorlevel%"=="0" goto :no_msbuild

if defined VxKex_NEXT_Platform_Toolset goto :toolset_ready
echo Select platform toolset:
echo 1 - Windows SDK 7.1 ^(Windows7.1SDK^)
echo 2 - Visual Studio 2010 ^(v100^)
echo 3 - Visual Studio 2012 Windows XP ^(v110_xp^)
echo 4 - Visual Studio 2013 Windows XP ^(v120_xp^)
echo 5 - Visual Studio 2015 Windows XP ^(v140_xp^)
set "choice="
set /p "choice=Enter a number and press Enter: "
if "%choice%"=="1" set "VxKex_NEXT_Platform_Toolset=Windows7.1SDK"
if "%choice%"=="2" set "VxKex_NEXT_Platform_Toolset=v100"
if "%choice%"=="3" set "VxKex_NEXT_Platform_Toolset=v110_xp"
if "%choice%"=="4" set "VxKex_NEXT_Platform_Toolset=v120_xp"
if "%choice%"=="5" set "VxKex_NEXT_Platform_Toolset=v140_xp"
if defined VxKex_NEXT_Platform_Toolset goto :toolset_ready
echo Invalid choice, defaulting to v100.
set "VxKex_NEXT_Platform_Toolset=v100"

:toolset_ready
echo Using platform toolset: %VxKex_NEXT_Platform_Toolset%

REM Never leave an old installer looking like the result of a failed build.
del /F /Q "%~dp0..\KexSetup_Release.exe" >nul 2>nul
if exist "%~dp0..\KexSetup_Release.exe" goto :missing_output

call SetEnv.Cmd /x64
if not "%errorlevel%"=="0" goto :failed
MSBuild.exe "%~dp0..\VxKex.sln" /p:Configuration=Release /p:Platform="x64" /p:PlatformToolset=%VxKex_NEXT_Platform_Toolset%
if not "%errorlevel%"=="0" goto :failed

call SetEnv.Cmd /x86
if not "%errorlevel%"=="0" goto :failed
MSBuild.exe "%~dp0..\VxKex.sln" /p:Configuration=Release /p:Platform="Win32" /p:PlatformToolset=%VxKex_NEXT_Platform_Toolset%
if not "%errorlevel%"=="0" goto :failed

if not exist "%~dp0..\KexSetup_Release.exe" goto :missing_output
for %%F in ("%~dp0..\KexSetup_Release.exe") do if %%~zF EQU 0 goto :missing_output
endlocal & exit /b 0

:failed
set "BUILD_EXIT=%errorlevel%"
goto :failure_cleanup

:no_msbuild
echo ERROR: Run this script in a Visual Studio or Windows SDK Command Prompt.
set "BUILD_EXIT=1"
goto :failure_cleanup

:missing_output
echo ERROR: Could not replace or create KexSetup_Release.exe.
set "BUILD_EXIT=1"

:failure_cleanup
del /F /Q "%~dp0..\KexSetup_Release.exe" >nul 2>nul
echo ERROR: Release build failed with exit code %BUILD_EXIT%.
if not defined VxKex_NEXT_NOPAUSE pause
endlocal & exit /b %BUILD_EXIT%
