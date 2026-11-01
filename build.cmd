@echo off
rem Builds the launcher, loader and mods into bin\ (32-bit, matching the game), and the editor
rem into bin\editor\.
setlocal
set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio C++ tools not found & exit /b 1)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1

if not exist "%ROOT%bin\mods" mkdir "%ROOT%bin\mods"
if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%bin\rfa_mods.ini" copy /y "%ROOT%config\rfa_mods.ini" "%ROOT%bin\rfa_mods.ini" >nul

set CFLAGS=/nologo /std:c++17 /permissive- /O2 /MT /W3 /EHsc /DUNICODE /D_UNICODE /I"%ROOT%src\common"
pushd "%ROOT%build"

cl %CFLAGS% "%ROOT%src\launcher\launcher.cpp" /Fe"%ROOT%bin\RFAModLauncher.exe" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib || goto :fail
cl %CFLAGS% /LD "%ROOT%src\loader\loader.cpp" /Fe"%ROOT%bin\rfa_loader.dll" /link /IMPLIB:rfa_loader.lib user32.lib || goto :fail
cl %CFLAGS% /LD "%ROOT%src\mods\borderless\borderless.cpp" /Fe"%ROOT%bin\mods\borderless.dll" /link /IMPLIB:borderless.lib user32.lib || goto :fail
cl %CFLAGS% /LD "%ROOT%src\mods\fov\fov.cpp" /Fe"%ROOT%bin\mods\fov.dll" /link /IMPLIB:fov.lib user32.lib || goto :fail
cl %CFLAGS% /LD "%ROOT%src\mods\video_settings\video_settings.cpp" /Fe"%ROOT%bin\mods\video_settings.dll" /link /IMPLIB:video_settings.lib || goto :fail
cl %CFLAGS% /LD "%ROOT%src\mods\console\console.cpp" /Fe"%ROOT%bin\mods\console.dll" /link /IMPLIB:console.lib || goto :fail
cl %CFLAGS% /LD "%ROOT%src\mods\guerrilla_world\guerrilla_world.cpp" /Fe"%ROOT%bin\mods\guerrilla_world.dll" /link /IMPLIB:guerrilla_world.lib || goto :fail
cl %CFLAGS% /LD "%ROOT%src\mods\guerrilla_hud\guerrilla_hud.cpp" /Fe"%ROOT%bin\mods\guerrilla_hud.dll" /link /IMPLIB:guerrilla_hud.lib || goto :fail
rem guerrilla_hud's scripts, which editor\GuerrillaHud.exe packs with Guerrilla's HUD documents.
xcopy /i /y /q "%ROOT%src\mods\guerrilla_hud\*.lua" "%ROOT%bin\mods\guerrilla_hud" >nul || goto :fail

popd
rem overrides\ mirrors the game folder (one folder per archive), so copy the whole tree.
xcopy /e /i /y /q "%ROOT%src\overrides" "%ROOT%bin\overrides" >nul || goto :fail
rem console.ini is yours to edit: only copy the template if there is none yet.
if not exist "%ROOT%bin\console.ini" copy /y "%ROOT%config\console.ini" "%ROOT%bin\console.ini" >nul

rem The editor is C# and needs the .NET 10 SDK. Its build leftovers go to build\editor. The
rem DOTNET_* settings turn off the SDK's usage reporting and its first-run web certificate.
set DOTNET_CLI_TELEMETRY_OPTOUT=1
set DOTNET_NOLOGO=1
set DOTNET_GENERATE_ASPNET_CERTIFICATE=false
rem vcvarsall (for the 32-bit C++ parts above) sets Platform=x86, which MSBuild would take for the
rem editor too and build it 32-bit, with 2 GB of memory: too little for big levels. Clear it.
set Platform=
where dotnet >nul 2>nul || (echo Skipping the editor: the .NET 10 SDK is not installed & goto :done)
dotnet build "%ROOT%src\editor\RfaEditor\RfaEditor.csproj" -c Release -o "%ROOT%bin\editor" --artifacts-path "%ROOT%build\editor" -v q -nologo || goto :fail
rem GuerrillaHud.exe, next to the editor: makes Guerrilla's HUD for guerrilla_hud from your copy of Guerrilla.
dotnet build "%ROOT%src\editor\GuerrillaHud\GuerrillaHud.csproj" -c Release -o "%ROOT%bin\editor" --artifacts-path "%ROOT%build\editor" -v q -nologo || goto :fail
rem GuerrillaWorld.exe, next to the editor: builds the world rfg for guerrilla_world from your copy of Guerrilla.
dotnet build "%ROOT%src\editor\GuerrillaWorld\GuerrillaWorld.csproj" -c Release -o "%ROOT%bin\editor" --artifacts-path "%ROOT%build\editor" -v q -nologo || goto :fail

:done
echo Build OK: %ROOT%bin
exit /b 0
:fail
popd
echo BUILD FAILED
exit /b 1
