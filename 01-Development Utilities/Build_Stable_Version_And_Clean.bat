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

set "BACKUP_DIR=%TEMP%\VxKex-vautogen-%RANDOM%-%RANDOM%"
if exist "%BACKUP_DIR%" goto :backup_collision
mkdir "%BACKUP_DIR%"
if not "%errorlevel%"=="0" goto :failed
copy /Y "%~dp0..\00-Common Headers\vautogen.h" "%BACKUP_DIR%" >nul
if not "%errorlevel%"=="0" goto :failed
copy /Y "%~dp0vautogen\vautogen.ini" "%BACKUP_DIR%" >nul
if not "%errorlevel%"=="0" goto :failed

call "%~dp0Build_Debug.bat"
set "DEBUG_EXIT=%errorlevel%"
REM Debug packaging increments the version files, even if a later step fails.
REM Always restore the originals before returning or starting Release.
call :restore_version
set "RESTORE_EXIT=%errorlevel%"
if not "%DEBUG_EXIT%"=="0" goto :debug_failed
if not "%RESTORE_EXIT%"=="0" goto :restore_failed

call "%~dp0Build_Release.bat"
if not "%errorlevel%"=="0" goto :failed
call "%~dp0Clean_VxKex_NEXT.bat"
if not "%errorlevel%"=="0" goto :failed
endlocal & exit /b 0

:restore_version
set "RESTORE_STATUS=0"
copy /Y "%BACKUP_DIR%\vautogen.h" "%~dp0..\00-Common Headers" >nul || set "RESTORE_STATUS=1"
copy /Y "%BACKUP_DIR%\vautogen.ini" "%~dp0vautogen" >nul || set "RESTORE_STATUS=1"
if not "%RESTORE_STATUS%"=="0" exit /b %RESTORE_STATUS%
rmdir /S /Q "%BACKUP_DIR%"
if exist "%BACKUP_DIR%" exit /b 1
set "BACKUP_DIR="
exit /b 0

:debug_failed
set "BUILD_EXIT=%DEBUG_EXIT%"
goto :failure_cleanup

:restore_failed
set "BUILD_EXIT=%RESTORE_EXIT%"
goto :failure_cleanup

:failed
set "BUILD_EXIT=%errorlevel%"
goto :failure_cleanup

:backup_collision
echo ERROR: Backup directory already exists: "%BACKUP_DIR%".
set "BUILD_EXIT=1"
goto :failure_cleanup

:no_msbuild
echo ERROR: Run this script in a Visual Studio or Windows SDK Command Prompt.
set "BUILD_EXIT=1"

:failure_cleanup
del /F /Q "%~dp0..\KexSetup_Debug.exe" "%~dp0..\KexSetup_Release.exe" >nul 2>nul
if defined BACKUP_DIR echo Version backup retained at "%BACKUP_DIR%".
echo ERROR: Build stopped with exit code %BUILD_EXIT%.
if not defined VxKex_NEXT_NOPAUSE pause
endlocal & exit /b %BUILD_EXIT%
