@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "CLEAN_EXIT=0"
pushd "%~dp0.."
if not "%errorlevel%"=="0" goto :push_failed

for /F "delims=" %%d in ('dir /AD /B /S 2^>nul') do (
    for %%a in (ipch .vs x64 Debug Release) do (
        if /I "%%~nxd"=="%%a" (
            if /I not "%%d"=="%cd%\02-Prebuilt DLLs\x64" call :remove_directory "%%d"
        )
    )
)
for /F "delims=" %%f in ('dir /A-D /B /S debug.log *.aps *.cache *.sdf *.suo *.user *.VC.db *.VC.opendb 2^>nul') do call :remove_file "%%f"
for /F "delims=" %%f in ('dir /A-D /B /S "02-Prebuilt Data\*.pdb" 2^>nul') do call :remove_file "%%f"
for /F "delims=" %%f in ('dir /A-D /B /S "02-Prebuilt Data\KexDir\Globalization\Dictionaries\*" 2^>nul') do call :remove_file "%%f"
call :remove_file "01-Development Utilities\moz2sst\moz2sst.exe"
call :remove_file "01-Development Utilities\moz2sst\moz2sst.pdb"
call :remove_file "01-Development Utilities\moz2sst\ROOT.sst"
popd
if not defined VxKex_NEXT_NOPAUSE pause
endlocal & exit /b %CLEAN_EXIT%

:remove_directory
if not exist "%~1" exit /b 0
echo Deleting directory "%~1".
rmdir /S /Q "%~1"
if exist "%~1" set "CLEAN_EXIT=1"
exit /b 0

:remove_file
if not exist "%~1" exit /b 0
echo Deleting file "%~1".
del /A /F /Q "%~1"
if exist "%~1" set "CLEAN_EXIT=1"
exit /b 0

:push_failed
set "CLEAN_EXIT=%errorlevel%"
echo ERROR: Cannot enter the project directory.
if not defined VxKex_NEXT_NOPAUSE pause
endlocal & exit /b %CLEAN_EXIT%
