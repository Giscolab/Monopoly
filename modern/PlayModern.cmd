@echo off
setlocal
set "MONOPOLY_PLAY_ROOT=%~dp0"
rem One canonical playable build: never silently select an older Debug/Release binary.
set "MONOPOLY_PLAY_CONFIG=Release"
set "MONOPOLY_PLAY_DIR=%MONOPOLY_PLAY_ROOT%build\%MONOPOLY_PLAY_CONFIG%"
set "MONOPOLY_PLAY_EXE=%MONOPOLY_PLAY_DIR%\MonopolyModern.exe"
rem Build the canonical Release before every launch. Never run stale C++/HLSL.
set "MONOPOLY_CMAKE=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%MONOPOLY_CMAKE%" set "MONOPOLY_CMAKE=cmake"
echo [MonopolyModern] Synchronisation de la version Release...
"%MONOPOLY_CMAKE%" --build "%MONOPOLY_PLAY_ROOT%build" --config Release --target MonopolyModern --parallel 4
if errorlevel 1 (
    echo [MonopolyModern] Echec de compilation. Aucun ancien executable ne sera lance.
    pause
    exit /b 1
)
if not exist "%MONOPOLY_PLAY_EXE%" (
    echo Build MonopolyModern first: cmake --build modern/build --config Release --target MonopolyModern
    pause
    exit /b 1
)
if not exist "%MONOPOLY_PLAY_DIR%\assets\modern\board\usa_procedural_runtime.glb" (
    echo Export the procedural gameplay scene first. See PROCEDURAL_PLAY.md.
    pause
    exit /b 1
)
if not exist "%MONOPOLY_PLAY_ROOT%..\runtime-data\Dat_Mon\dat_main.dat" (
    echo Place the licensed retail USA data in runtime-data. See PROCEDURAL_PLAY.md.
    pause
    exit /b 1
)
rem Preserve existing Release saves. Migrate Debug saves only on first Release launch.
if "%MONOPOLY_PLAY_CONFIG%"=="Release" if not exist "%MONOPOLY_PLAY_DIR%\savegame" if exist "%MONOPOLY_PLAY_ROOT%build\Debug\savegame\game*.msv" (
    xcopy "%MONOPOLY_PLAY_ROOT%build\Debug\savegame" "%MONOPOLY_PLAY_DIR%\savegame\" /E /I /Q >nul
    if errorlevel 1 (
        echo Unable to copy existing saves. Original saves remain in build\Debug\savegame.
        pause
        exit /b 1
    )
)
rem Full Help uses an isolated optional exporter; preserve user configuration.
if not defined MONOPOLY_WINHLP if exist "%MONOPOLY_PLAY_ROOT%build\help-export-tools\Scripts\winhlp.exe" set "MONOPOLY_WINHLP=%MONOPOLY_PLAY_ROOT%build\help-export-tools\Scripts\winhlp.exe"
if not defined MONOPOLY_WINHLP (
    echo Full Help needs winhlp on PATH or the build-local exporter. See FULL_HELP.md.
)
cd /d "%MONOPOLY_PLAY_DIR%"
start "" "%MONOPOLY_PLAY_EXE%" --windowed --resolution 1920x1080 --present-mode vsync --edition=usa --language=en-us --modern-board=procedural --modern-environment=procedural --modern-buildings=house --ui=vintage --data-root "%MONOPOLY_PLAY_ROOT%..\runtime-data" %*
endlocal
