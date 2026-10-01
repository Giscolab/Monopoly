@echo off
setlocal
set "MONOPOLY_PLAY_ROOT=%~dp0"
set "MONOPOLY_PLAY_EXE=%MONOPOLY_PLAY_ROOT%build\Debug\MonopolyModern.exe"
if not exist "%MONOPOLY_PLAY_EXE%" (
    echo Build MonopolyModern first: cmake --build modern/build --config Debug --target MonopolyModern
    pause
    exit /b 1
)
if not exist "%MONOPOLY_PLAY_ROOT%build\Debug\assets\modern\board\usa_procedural_runtime.glb" (
    echo Export the procedural gameplay scene first. See PROCEDURAL_PLAY.md.
    pause
    exit /b 1
)
if not exist "%MONOPOLY_PLAY_ROOT%..\runtime-data\Dat_Mon\dat_main.dat" (
    echo Place the licensed retail USA data in runtime-data. See PROCEDURAL_PLAY.md.
    pause
    exit /b 1
)
cd /d "%MONOPOLY_PLAY_ROOT%build\Debug"
start "" "%MONOPOLY_PLAY_EXE%" --windowed --resolution 1920x1080 --present-mode vsync --edition=usa --language=en-us --modern-board=procedural --modern-environment=procedural --modern-buildings=house --data-root "%MONOPOLY_PLAY_ROOT%..\runtime-data"
endlocal
