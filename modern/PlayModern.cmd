@echo off
setlocal
set "MONOPOLY_PLAY_ROOT=%~dp0"
set "MONOPOLY_PLAY_CONFIG=Debug"
if exist "%MONOPOLY_PLAY_ROOT%build\Release\MonopolyModern.exe" if exist "%MONOPOLY_PLAY_ROOT%build\Release\assets\modern\board\usa_procedural_runtime.glb" set "MONOPOLY_PLAY_CONFIG=Release"
set "MONOPOLY_PLAY_DIR=%MONOPOLY_PLAY_ROOT%build\%MONOPOLY_PLAY_CONFIG%"
set "MONOPOLY_PLAY_EXE=%MONOPOLY_PLAY_DIR%\MonopolyModern.exe"
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
start "" "%MONOPOLY_PLAY_EXE%" --windowed --resolution 1920x1080 --present-mode vsync --edition=usa --language=en-us --modern-board=procedural --modern-environment=procedural --modern-buildings=house --data-root "%MONOPOLY_PLAY_ROOT%..\runtime-data"
endlocal
