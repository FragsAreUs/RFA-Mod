@echo off
rem Builds the launcher, loader and mods into bin\ (32-bit, matching the game), and the editor
rem into bin\editor\.
rem   build.cmd             into bin\, the copy you play
rem   build.cmd <folder>    into another folder instead, such as a staging copy of bin\ that
rem                         tools\stage.ps1 makes (stage\<name>\): its build leftovers go to
rem                         build\stage-<name>\, so several builds can run at once.
rem What the compilers say goes to build\build.log (shown if the build fails); a build into another
rem folder keeps its own, build\stage-<name>\build.log, so two builds at once don't share one.
rem
rem The project folder is reached through a drive letter of its own (subst) while it builds: MSBuild
rem can't copy into a folder whose path has an apostrophe (C:\Users\Jo's PC\...), and Visual Studio's
rem vcvarsall.bat can stop there too. vcvarsall runs in a cmd of its own, from that drive, and the
rem settings it makes are taken from there.
setlocal
set "HERE=%~dp0"
set "STAGE=%~1"
set "SHOWN=%~dp0bin"
if not "%~1"=="" set "SHOWN=%~f1"
set "STAGENAME=%~nx1"
if not "%STAGE%"=="" set "STAGE=%~f1"
set "LOGDIR=%HERE%build"
if not "%STAGE%"=="" set "LOGDIR=%HERE%build\stage-%STAGENAME%"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOG=%LOGDIR%\build.log"
echo Build %DATE% %TIME% > "%LOG%"

rem The MCP servers Claude runs from bin\ (bin\live_analysis_mcp.exe, bin\editor\RfaEditorMcp.exe) keep their
rem files locked, so a build into bin\ closes them first; Claude starts them again when it next uses them.
if "%STAGE%"=="" (
    taskkill /f /im RfaEditorMcp.exe >> "%LOG%" 2>&1
    taskkill /f /im live_analysis_mcp.exe >> "%LOG%" 2>&1
)

rem Visual Studio's C++ tools: vswhere (any edition, Build Tools too, previews too), else the usual folders.
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -property installationPath`) do if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*" "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do for /d %%e in ("%%~v\*") do if exist "%%~e\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%~e\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS (echo Visual Studio C++ tools not found: no vcvarsall.bat under Program Files\Microsoft Visual Studio & exit /b 1)
echo vcvarsall: %VCVARS% >> "%LOG%"

set "DRIVE="
rem Each free-looking letter is tried in turn, and the first one that really maps to this folder is kept.
rem A letter can look free and still be refused ("Drive already SUBSTed": a build stopped before it let
rem go, its folder has moved since, or another window made it), so a refusal just moves on to the next.
for %%d in (R S T U V W X Y Z Q P O N M L K J I H G) do if not defined DRIVE if not exist %%d:\ (subst %%d: "%HERE:~0,-1%" >> "%LOG%" 2>&1 & if exist %%d:\build.cmd set "DRIVE=%%d:")
if not defined DRIVE (echo No free drive letter to build through, see %LOG% & exit /b 1)
if not exist %DRIVE%\build.cmd (echo Couldn't map %DRIVE% to the project folder & exit /b 1)
set "ROOT=%DRIVE%\"
set "OUT=%ROOT%bin"
set "OBJ=%ROOT%build"
if not "%STAGE%"=="" set "OUT=%STAGE%"
if not "%STAGE%"=="" set "OBJ=%ROOT%build\stage-%STAGENAME%"
cd /d %ROOT%

rem vcvarsall x86 in a cmd of its own; its settings (set) into a file, then into this one.
rem What vcvarsall says goes to a file of its own first, and it is told not to start its usage
rem report: that helper keeps running with the file it was writing to still open, and the log must
rem stay free for the compilers below.
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "ENVFILE=%LOGDIR%\vcvars-env.txt"
cmd /d /c ""%VCVARS%" x86 > "%LOGDIR%\vcvars.log" 2>&1 && set > "%ENVFILE%""
type "%LOGDIR%\vcvars.log" >> "%LOG%" 2>nul
if not exist "%ENVFILE%" (echo vcvarsall failed >> "%LOG%" & goto :fail_unmapped)
for /f "usebackq tokens=1* delims==" %%a in ("%ENVFILE%") do set "%%a=%%b"
del "%ENVFILE%" >nul 2>&1
where cl >> "%LOG%" 2>&1 || (echo vcvarsall didn't give the C++ compiler cl.exe >> "%LOG%" & goto :fail_unmapped)

if not exist "%OUT%\mods" mkdir "%OUT%\mods"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%\rfa_mods.ini" copy /y "%ROOT%config\rfa_mods.ini" "%OUT%\rfa_mods.ini" >nul

set CFLAGS=/nologo /std:c++17 /permissive- /O2 /MT /W3 /EHsc /DUNICODE /D_UNICODE /I"%ROOT%src\common"
pushd "%OBJ%"

echo Building the launcher, loader and mods ...
cl %CFLAGS% "%ROOT%src\launcher\launcher.cpp" /Fe"%OUT%\RFAModLauncher.exe" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib >> "%LOG%" 2>&1 || goto :fail
rem Dear ImGui (src\third_party\imgui), for the in-game menus: the loader hosts it with its
rem Direct3D 11 and Win32 backends; a mod with menus is built with its core files. NDEBUG leaves out
rem its debug checks, which would stop the game with a dialog.
set "IMGUI=%ROOT%src\third_party\imgui"
set IMGUI_CORE="%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp"
rem Each DLL of ours gets a linker map next to it (rfa_loader.map, mods\*.map): the functions' addresses, which
rem name the addresses the loader's profiler ([Loader] Profile=1) samples.
cl %CFLAGS% /DNDEBUG /I"%IMGUI%" /LD "%ROOT%src\loader\loader.cpp" "%ROOT%src\loader\menus.cpp" "%ROOT%src\loader\profiler.cpp" %IMGUI_CORE% "%IMGUI%\backends\imgui_impl_dx11.cpp" "%IMGUI%\backends\imgui_impl_win32.cpp" /Fe"%OUT%\rfa_loader.dll" /link /IMPLIB:rfa_loader.lib /MAP:"%OUT%\rfa_loader.map" user32.lib >> "%LOG%" 2>&1 || goto :fail
rem rfa_qol.dll: the quality-of-life mods (borderless, console, fov, video_settings, spawner), one
rem source file each, in one DLL. The loader skips their old DLLs if an older build left them in bin\mods.
cl %CFLAGS% /DNDEBUG /I"%IMGUI%" /LD "%ROOT%src\mods\rfa_qol\*.cpp" %IMGUI_CORE% /Fe"%OUT%\mods\rfa_qol.dll" /link /IMPLIB:rfa_qol.lib /MAP:"%OUT%\mods\rfa_qol.map" user32.lib psapi.lib dbghelp.lib >> "%LOG%" 2>&1 || goto :fail
rem live_analysis_mcp.exe: standalone MCP server for live analysis
cl %CFLAGS% "%ROOT%src\mods\rfa_qol\live_analysis_mcp.cpp" /Fe"%OUT%\live_analysis_mcp.exe" >> "%LOG%" 2>&1 || goto :fail
rem rfa_guerrilla.dll: the mods for Guerrilla's world and campaign, nine parts in one DLL (src\mods\guerrilla\:
rem campaign, gear, hud, kiosks, people, save, vehicles, voices, world). Each part compiles into a folder of its
rem own as it did when it was a DLL of its own (two parts have a tables.cpp), gear and hud with ImGui for their
rem F7 windows; then one link, with ImGui's core files once. The loader skips the parts' old DLLs
rem (guerrilla_world.dll, ...) if an older build left them in bin\mods.
set "GUERRILLA=%ROOT%src\mods\guerrilla"
set "GOBJ=%OBJ%\rfa_guerrilla"
if exist "%GOBJ%" rd /s /q "%GOBJ%"
for %%p in (campaign kiosks people save vehicles voices world) do (
    mkdir "%GOBJ%\%%p"
    cl %CFLAGS% /I"%GUERRILLA%" /c "%GUERRILLA%\%%p\*.cpp" /Fo"%GOBJ%\%%p\\" >> "%LOG%" 2>&1 || goto :fail
)
for %%p in (gear hud) do (
    mkdir "%GOBJ%\%%p"
    cl %CFLAGS% /DNDEBUG /I"%IMGUI%" /I"%GUERRILLA%" /c "%GUERRILLA%\%%p\*.cpp" /Fo"%GOBJ%\%%p\\" >> "%LOG%" 2>&1 || goto :fail
)
mkdir "%GOBJ%\main"
cl %CFLAGS% /I"%GUERRILLA%" /c "%GUERRILLA%\rfa_guerrilla.cpp" /Fo"%GOBJ%\main\\" >> "%LOG%" 2>&1 || goto :fail
cl %CFLAGS% /DNDEBUG /I"%IMGUI%" /c %IMGUI_CORE% /Fo"%GOBJ%\main\\" >> "%LOG%" 2>&1 || goto :fail
cl %CFLAGS% /LD "%GOBJ%\main\*.obj" "%GOBJ%\campaign\*.obj" "%GOBJ%\gear\*.obj" "%GOBJ%\hud\*.obj" "%GOBJ%\kiosks\*.obj" "%GOBJ%\people\*.obj" "%GOBJ%\save\*.obj" "%GOBJ%\vehicles\*.obj" "%GOBJ%\voices\*.obj" "%GOBJ%\world\*.obj" /Fe"%OUT%\mods\rfa_guerrilla.dll" /link /IMPLIB:rfa_guerrilla.lib /MAP:"%OUT%\mods\rfa_guerrilla.map" user32.lib >> "%LOG%" 2>&1 || goto :fail
rem guerrilla_hud's scripts, which editor\GuerrillaBuilder.exe packs with Guerrilla's HUD documents (the part's
rem data folder keeps its old name).
xcopy /i /y /q "%GUERRILLA%\hud\*.lua" "%OUT%\mods\guerrilla_hud" >nul || goto :fail

popd
rem overrides\ mirrors the game folder (one folder per archive), so copy the whole tree.
xcopy /e /i /y /q "%ROOT%src\overrides" "%OUT%\overrides" >nul || goto :fail_unpopped
rem console.ini is yours to edit: only copy the template if there is none yet.
if not exist "%OUT%\console.ini" copy /y "%ROOT%config\console.ini" "%OUT%\console.ini" >nul
rem The templates again, in bin\defaults\: each time it starts, the launcher adds the settings they
rem have and your bin\rfa_mods.ini and bin\console.ini lack, keeping your values.
if not exist "%OUT%\defaults" mkdir "%OUT%\defaults"
copy /y "%ROOT%config\rfa_mods.ini" "%OUT%\defaults\rfa_mods.ini" >nul || goto :fail_unpopped
copy /y "%ROOT%config\console.ini" "%OUT%\defaults\console.ini" >nul || goto :fail_unpopped

rem The editor is C# and needs the .NET 10 SDK. Its build leftovers go to build\editor, made afresh
rem each time (they keep the path they were made through, and the drive letter can change). The
rem DOTNET_* settings turn off the SDK's usage reporting and its first-run web certificate.
set DOTNET_CLI_TELEMETRY_OPTOUT=1
set DOTNET_NOLOGO=1
set DOTNET_GENERATE_ASPNET_CERTIFICATE=false
rem vcvarsall (for the 32-bit C++ parts above) sets Platform=x86, which MSBuild would take for the
rem editor too and build it 32-bit, with 2 GB of memory: too little for big levels. Clear it.
set Platform=
where dotnet >nul 2>nul || (echo Skipping the editor: the .NET 10 SDK is not installed & goto :done)
echo Building the editor ...
if exist "%OBJ%\editor" rd /s /q "%OBJ%\editor"
dotnet build "%ROOT%src\editor\RfaEditor\RfaEditor.csproj" -c Release -o "%OUT%\editor" --artifacts-path "%OBJ%\editor" -v q -nologo -nodeReuse:false >> "%LOG%" 2>&1 || goto :fail_unpopped
rem GuerrillaBuilder.exe, next to the editor: makes Guerrilla's world, HUD, vehicles, weapons and people
rem usable from your copy of Guerrilla. It took the place of four programs, whose files go from bin\editor.
for %%t in (GuerrillaWorld GuerrillaHud GuerrillaArsenal GuerrillaPeople) do for %%e in (exe dll deps.json runtimeconfig.json pdb) do if exist "%OUT%\editor\%%t.%%e" del /q "%OUT%\editor\%%t.%%e"
dotnet build "%ROOT%src\editor\GuerrillaBuilder\GuerrillaBuilder.csproj" -c Release -o "%OUT%\editor" --artifacts-path "%OBJ%\editor" -v q -nologo -nodeReuse:false >> "%LOG%" 2>&1 || goto :fail_unpopped
rem RfaEditorMcp.exe: MCP server for the editor
dotnet build "%ROOT%src\editor\RfaEditorMcp\RfaEditorMcp.csproj" -c Release -o "%OUT%\editor" --artifacts-path "%OBJ%\editor" -v q -nologo -nodeReuse:false >> "%LOG%" 2>&1 || goto :fail_unpopped

:done
cd /d "%HERE%"
subst %DRIVE% /d
echo Build OK: %SHOWN%
exit /b 0
:fail
popd
:fail_unpopped
:fail_unmapped
cd /d "%HERE%"
subst %DRIVE% /d
type "%LOG%"
echo BUILD FAILED (build\build.log)
exit /b 1
