@echo off
rem Installs LYNX's plugins for other hosts on Windows, in the folders every host scans:
rem   CLAP -> %CommonProgramFiles%\CLAP   VST3 -> %CommonProgramFiles%\VST3
rem Those folders need administrator rights: right-click this file and choose "Run as administrator".
rem "install.cmd /uninstall" removes exactly what it installed (the names in PLUGINS.txt).
setlocal
set "HERE=%~dp0"
set "CLAPDIR=%CommonProgramFiles%\CLAP"
set "VST3DIR=%CommonProgramFiles%\VST3"
net session >nul 2>&1
if errorlevel 1 (
    echo This needs administrator rights: right-click install.cmd and choose "Run as administrator".
    pause
    exit /b 1
)
if /i "%~1"=="/uninstall" (
    if exist "%CLAPDIR%\LYNX Plugins.clap" del /f /q "%CLAPDIR%\LYNX Plugins.clap"
    for /d %%B in ("%HERE%VST3\*.vst3") do if exist "%VST3DIR%\%%~nxB" rmdir /s /q "%VST3DIR%\%%~nxB"
    echo LYNX plugins removed.
    pause
    exit /b 0
)
if not exist "%CLAPDIR%" mkdir "%CLAPDIR%"
if not exist "%VST3DIR%" mkdir "%VST3DIR%"
copy /y "%HERE%CLAP\LYNX Plugins.clap" "%CLAPDIR%\" >nul || goto failed
for /d %%B in ("%HERE%VST3\*.vst3") do (
    if exist "%VST3DIR%\%%~nxB" rmdir /s /q "%VST3DIR%\%%~nxB"
    xcopy /e /i /q /y "%%B" "%VST3DIR%\%%~nxB" >nul || goto failed
)
echo LYNX plugins installed: CLAP in %CLAPDIR%, VST3 in %VST3DIR%. Rescan the plugins in your host.
pause
exit /b 0
:failed
echo Copying failed. Close every program that has the LYNX plugins loaded, then try again.
pause
exit /b 1
