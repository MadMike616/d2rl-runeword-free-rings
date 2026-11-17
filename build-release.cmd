@echo off
setlocal
if "%~1"=="" (
    echo Usage: build-release.cmd ^<output-directory^>
    exit /b 2
)
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
    exit /b 2
)
set "RINGS_OUTPUT=%~f1"
set "RINGS_SDK=%~dp0PluginSDK\SDK-0.4.0\include"
if not exist "%RINGS_SDK%\D2RLPlugin\api.h" set "RINGS_SDK=%~dp0..\PluginSDK\SDK-0.4.0\include"
if not exist "%RINGS_SDK%\D2RLPlugin\api.h" (
    echo D2RLoader Plugin SDK 0.4.0 headers were not found in or beside the source folder.
    exit /b 2
)
if not exist "%RINGS_OUTPUT%" mkdir "%RINGS_OUTPUT%"
python -B "%~dp0build-install-data.py" "%RINGS_OUTPUT%\install-data.hpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /O2 /DNDEBUG /MD /W4 /WX /permissive- /EHsc /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I"%RINGS_SDK%" /c "%~dp0src\plugin.cpp" /Fo"%RINGS_OUTPUT%\plugin.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /O2 /DNDEBUG /MD /W4 /WX /permissive- /EHsc /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I"%RINGS_SDK%" /I"%RINGS_OUTPUT%" /c "%~dp0src\installer.cpp" /Fo"%RINGS_OUTPUT%\installer.obj"
if errorlevel 1 exit /b %errorlevel%
rc.exe /nologo /I"%RINGS_SDK%" /fo"%RINGS_OUTPUT%\plugin.res" "%~dp0src\plugin.rc"
if errorlevel 1 exit /b %errorlevel%
link.exe /nologo /DLL /INCREMENTAL:NO /Brepro /MACHINE:X64 /MAP:"%RINGS_OUTPUT%\d2rl-runeword-free-rings.map" /OUT:"%RINGS_OUTPUT%\d2rl-runeword-free-rings.dll" "%RINGS_OUTPUT%\plugin.obj" "%RINGS_OUTPUT%\installer.obj" "%RINGS_OUTPUT%\plugin.res"
exit /b %errorlevel%
