@echo off
chcp 65001 >nul
setlocal
if exist "%~dp0x64\Release\avformat-61.dll" if exist "%~dp0x64\Release\platforms\qwindows.dll" goto local_runtime
set "DCTS_QT_ROOT=%QtInstall%"
if not defined DCTS_QT_ROOT set "DCTS_QT_ROOT=%CXWL_QT_ROOT%"
if not defined DCTS_QT_ROOT goto missing_runtime
if not exist "%DCTS_QT_ROOT%\bin\avformat-61.dll" goto missing_runtime
if not exist "%DCTS_QT_ROOT%\plugins\platforms\qwindows.dll" goto missing_runtime
set "PATH=%DCTS_QT_ROOT%\bin;%SystemRoot%\System32;%SystemRoot%"
set "QT_PLUGIN_PATH=%DCTS_QT_ROOT%\plugins"
goto launch
:missing_runtime
echo Build the Release runtime or set CXWL_QT_ROOT to a Qt 6.11.1 MSVC x64 installation. 1>&2
exit /b 1
:local_runtime
set "PATH=%~dp0x64\Release;%SystemRoot%\System32;%SystemRoot%"
set "QT_PLUGIN_PATH=%~dp0x64\Release"
:launch
cd /d "%~dp0"
start "" "%~dp0x64\Release\Cxwl Depth Camera.exe" %*
endlocal
