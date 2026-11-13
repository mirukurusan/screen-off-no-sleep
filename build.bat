@echo off
setlocal
cd /d "%~dp0"

where g++.exe >nul 2>nul
if %ERRORLEVEL% neq 0 (
    if exist "E:\Tools\mingw64\bin\g++.exe" (
        set "PATH=E:\Tools\mingw64\bin;%PATH%"
    )
)

echo [1/2] Compiling Windows Resources...
windres -I res -i res\resource.rc -o res\resource.o
if %ERRORLEVEL% neq 0 (
    echo Error: windres compilation failed.
    exit /b %ERRORLEVEL%
)

echo [2/2] Compiling C++ Executable...
g++ -O2 -std=c++17 -Wall -mwindows -municode ^
    src\main.cpp ^
    src\config.cpp ^
    src\power_manager.cpp ^
    src\input_listener.cpp ^
    src\hotkey_dialog.cpp ^
    res\resource.o ^
    -o screen-off-no-sleep.exe ^
    -lpowrprof -luser32 -lhid -lgdi32 -ladvapi32 -lshell32 -lcomctl32

if %ERRORLEVEL% equ 0 (
    echo Build succeeded: screen-off-no-sleep.exe
) else (
    echo Error: Compilation failed.
)
exit /b %ERRORLEVEL%
