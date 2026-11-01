@echo off
setlocal EnableExtensions DisableDelayedExpansion
if /I "%~1"=="/RELEASE" (set "DBGREL=Release") else (set "DBGREL=Debug")
set "PACKAGE_OUTPUT=%~dp0..\..\KexSetup_%DBGREL%.exe"
set "PACKAGE_PENDING=%~dp0..\..\KexSetup_%DBGREL%.pending"
pushd "%~dp0"
if not "%errorlevel%"=="0" goto :failed_no_push

REM Remove stale outputs before doing anything that can fail.
del /F /Q "%PACKAGE_OUTPUT%" "%PACKAGE_PENDING%" Archive.7z >nul 2>nul
if exist "%PACKAGE_OUTPUT%" goto :stale_output
if exist "%PACKAGE_PENDING%" goto :stale_output
if exist Archive.7z goto :stale_output
if exist Archive rmdir /S /Q Archive
if exist Archive goto :stale_output

for %%D in (Archive Archive\Core Archive\Core32 Archive\Core64 Archive\Kex32 Archive\Kex64) do (
    mkdir "%%D" || goto :failed
)
call :require_file "7zS2.sfx" || goto :failed
call :require_file "7zr.exe" || goto :failed
call :require_file "vtrplnt.exe" || goto :failed

REM Check the compiler result before POPD can replace its exit status.
call :compile_dictionaries || goto :failed

call :copy_required "..\..\%DBGREL%\KexSetup.exe" "Archive\" || goto :failed
call :copy_required "..\..\00-Documentation\Application Compatibility List.docx" "Archive\Core\" || goto :failed
call :copy_required "..\..\00-Documentation\Changelog.txt" "Archive\Core\" || goto :failed
call :require_file "..\..\02-Prebuilt Data\KexDir\Certificates\ROOT.sst" || goto :failed
xcopy "..\..\02-Prebuilt Data\KexDir\*" "Archive\Core\" /E /I /Y >nul
if not "%errorlevel%"=="0" goto :failed

REM Every built binary is mandatory in both architectures.
for %%F in (KexDll.dll KexShlEx.dll KexCfg.exe VxlView.exe CpiwBypa.dll CpiwBypaLdr.exe VxKexLdr.exe) do (
    call :copy_required "..\..\%DBGREL%\%%F" "Archive\Core32\" || goto :failed
    call :copy_required "..\..\x64\%DBGREL%\%%F" "Archive\Core64\" || goto :failed
)
for %%F in (KxAdvapi KxBase KxCom KxCrt KxCryp KxDw KxDx KxMi KxNet KxNt KxSchanl KxUac KxUia KxUser) do (
    call :copy_required "..\..\%DBGREL%\%%F.dll" "Archive\Kex32\" || goto :failed
    call :copy_required "..\..\x64\%DBGREL%\%%F.dll" "Archive\Kex64\" || goto :failed
)
REM The same-bitness UAC companion is loaded beside KxUac, not from Core.
call :copy_required "..\..\%DBGREL%\KexElevate.exe" "Archive\Kex32\" || goto :failed
call :copy_required "..\..\x64\%DBGREL%\KexElevate.exe" "Archive\Kex64\" || goto :failed

REM A wildcard copy alone would silently accept a missing runtime DLL.
for %%F in (dwrw10.dll icuuc.dll mshtmlmedia.dll msvcp_win.dll ucrtbase.dll) do (
    call :require_file "..\..\02-Prebuilt DLLs\x86\%%F" || goto :failed
)
for %%F in (dwrw10.dll icuuc.dll mfdevmgr.dll msvcp_win.dll ucrtbase.dll) do (
    call :require_file "..\..\02-Prebuilt DLLs\x64\%%F" || goto :failed
)
for %%F in ("..\..\02-Prebuilt DLLs\x86\*.dll") do (
    call :copy_required "%%~F" "Archive\Kex32\" || goto :failed
)
for %%F in ("..\..\02-Prebuilt DLLs\x64\*.dll") do (
    call :copy_required "%%~F" "Archive\Kex64\" || goto :failed
)

if /I not "%DBGREL%"=="Debug" goto :pack
call :copy_required "..\..\Debug\KexSetup.pdb" "Archive\" || goto :failed
for %%F in (KexDll KexShlEx KexCfg VxlView CpiwBypa CpiwBypaLdr VxKexLdr) do (
    call :copy_required "..\..\Debug\%%F.pdb" "Archive\Core32\" || goto :failed
    call :copy_required "..\..\x64\Debug\%%F.pdb" "Archive\Core64\" || goto :failed
)
for %%F in (KxAdvapi KxBase KxCom KxCrt KxCryp KxDw KxDx KxMi KxNet KxNt KxSchanl KxUac KxUia KxUser) do (
    call :copy_required "..\..\Debug\%%F.pdb" "Archive\Kex32\" || goto :failed
    call :copy_required "..\..\x64\Debug\%%F.pdb" "Archive\Kex64\" || goto :failed
)
call :copy_required "..\..\Debug\KexElevate.pdb" "Archive\Kex32\" || goto :failed
call :copy_required "..\..\x64\Debug\KexElevate.pdb" "Archive\Kex64\" || goto :failed

:pack
".\7zr.exe" a -y Archive.7z ".\Archive\*" -mmt1 -mx1 -m0=LZMA:d32
if not "%errorlevel%"=="0" goto :failed
call :require_file "Archive.7z" || goto :failed
".\7zr.exe" t Archive.7z
if not "%errorlevel%"=="0" goto :failed

REM vtrplnt expects this exact output name before appending the archive.
copy /Y "7zS2.sfx" "%PACKAGE_OUTPUT%" >nul
if not "%errorlevel%"=="0" goto :failed
".\vtrplnt.exe" /%DBGREL%
if not "%errorlevel%"=="0" goto :failed
copy /B "%PACKAGE_OUTPUT%"+Archive.7z "%PACKAGE_PENDING%" >nul
if not "%errorlevel%"=="0" goto :failed
call :require_file "%PACKAGE_PENDING%" || goto :failed
move /Y "%PACKAGE_PENDING%" "%PACKAGE_OUTPUT%" >nul
if not "%errorlevel%"=="0" goto :failed

if /I not "%DBGREL%"=="Debug" goto :clean
call :require_file "..\vautogen\vautogen.exe" || goto :failed
"..\vautogen\vautogen.exe"
if not "%errorlevel%"=="0" goto :failed

:clean
rmdir /S /Q Archive
if exist Archive goto :stale_output
del /F /Q Archive.7z
if exist Archive.7z goto :stale_output
popd
endlocal & exit /b 0

:compile_dictionaries
pushd "..\..\02-Prebuilt Data"
if not "%errorlevel%"=="0" exit /b %errorlevel%
if not exist "Dictionaries\*.dic" goto :dictionary_missing
call :require_file "mlsbdic.exe" || goto :dictionary_failed
if not exist "KexDir\Globalization\Dictionaries" mkdir "KexDir\Globalization\Dictionaries"
if not exist "KexDir\Globalization\Dictionaries" goto :dictionary_missing
for %%F in ("KexDir\Globalization\Dictionaries\*.bdi") do (
    if exist "%%~F" del /F /Q "%%~F"
    if exist "%%~F" goto :dictionary_missing
)
".\mlsbdic.exe" /IN:Dictionaries /OUT:KexDir\Globalization\Dictionaries
if not "%errorlevel%"=="0" goto :dictionary_failed
for %%F in ("Dictionaries\*.dic") do (
    call :require_file "KexDir\Globalization\Dictionaries\%%~nF.bdi" || goto :dictionary_failed
)
popd
exit /b 0

:dictionary_missing
echo ERROR: Dictionary sources or output directory are missing or locked.
popd
exit /b 1

:dictionary_failed
set "DICTIONARY_EXIT=%errorlevel%"
popd
exit /b %DICTIONARY_EXIT%

:copy_required
call :require_file "%~1" || exit /b 1
copy /Y "%~1" "%~2" >nul
exit /b %errorlevel%

:require_file
if not exist "%~1" (
    echo ERROR: Required file is missing: "%~1".
    exit /b 1
)
if %~z1 EQU 0 (
    echo ERROR: Required file is empty: "%~1".
    exit /b 1
)
exit /b 0

:stale_output
echo ERROR: Could not remove a stale packaging file or directory.
set "PACKAGE_EXIT=1"
goto :failure_cleanup

:failed
set "PACKAGE_EXIT=%errorlevel%"
goto :failure_cleanup

:failed_no_push
set "PACKAGE_EXIT=%errorlevel%"
echo ERROR: Cannot enter the packaging directory.
goto :report_failure

:failure_cleanup
del /F /Q "%PACKAGE_OUTPUT%" "%PACKAGE_PENDING%" Archive.7z >nul 2>nul
if exist Archive rmdir /S /Q Archive
popd

:report_failure
echo ERROR: Packaging failed with exit code %PACKAGE_EXIT%.
if not defined VxKex_NEXT_NOPAUSE pause
endlocal & exit /b %PACKAGE_EXIT%
