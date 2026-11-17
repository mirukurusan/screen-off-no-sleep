@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "TARGET_ARCH=%~1"
if "%TARGET_ARCH%"=="" set "TARGET_ARCH=all"

if /i "%TARGET_ARCH%"=="all" goto :build_all
if /i "%TARGET_ARCH%"=="x64" goto :build_x64
if /i "%TARGET_ARCH%"=="x86" goto :build_x86
if /i "%TARGET_ARCH%"=="arm64" goto :build_arm64

echo Unknown target architecture: %TARGET_ARCH%
echo Usage: %~nx0 [all^|x64^|x86^|arm64]
exit /b 1

:build_all
call :compile_target x64 x86_64-w64-mingw32 || exit /b !ERRORLEVEL!
call :compile_target x86 i686-w64-mingw32 || exit /b !ERRORLEVEL!
call :compile_target arm64 aarch64-w64-mingw32 || exit /b !ERRORLEVEL!
if !ERRORLEVEL! neq 0 (
    echo [Info] screen-off-no-sleep.exe is currently running; skipped copying x64 executable to default name.
)
echo.
echo All architectures built successfully.
exit /b 0

:build_x64
call :compile_target x64 x86_64-w64-mingw32 || exit /b !ERRORLEVEL!
if !ERRORLEVEL! neq 0 (
    echo [Info] screen-off-no-sleep.exe is currently running; skipped copying x64 executable to default name.
)
exit /b 0

:build_x86
call :compile_target x86 i686-w64-mingw32 || exit /b !ERRORLEVEL!
exit /b 0

:build_arm64
call :compile_target arm64 aarch64-w64-mingw32 || exit /b !ERRORLEVEL!
exit /b 0

:compile_target
set "ARCH=%~1"
set "PREFIX=%~2"
echo.
echo ========================================
echo Building %ARCH% (%PREFIX%)...
echo ========================================

where %PREFIX%-clang++ >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "RC_COMPILER=%PREFIX%-windres"
    set "CXX_COMPILER=%PREFIX%-clang++"
) else (
    if "%ARCH%"=="x64" (
        where g++ >nul 2>nul
        if %ERRORLEVEL% equ 0 (
            set "RC_COMPILER=windres"
            set "CXX_COMPILER=g++"
        ) else (
            echo Error: Compiler for %ARCH% not found.
            exit /b 1
        )
    ) else (
        echo Error: Compiler %PREFIX%-clang++ not found for %ARCH%.
        exit /b 1
    )
)

echo [1/2] Compiling Windows Resources for %ARCH%...
if not exist "res" mkdir res
%RC_COMPILER% -I res -i res\resource.rc -o res\resource-%ARCH%.o
if %ERRORLEVEL% neq 0 (
    echo Error: Resource compilation failed for %ARCH%.
    exit /b %ERRORLEVEL%
)

echo [2/2] Compiling %ARCH% Executable...
%CXX_COMPILER% -O2 -std=c++17 -Wall -mwindows -municode -static ^
    src\main.cpp ^
    src\config.cpp ^
    src\power_manager.cpp ^
    src\input_listener.cpp ^
    src\hotkey_dialog.cpp ^
    src\about_dialog.cpp ^
    res\resource-%ARCH%.o ^
    -o screen-off-no-sleep-%ARCH%.exe ^
    -lpowrprof -luser32 -lhid -lgdi32 -ladvapi32 -lshell32 -lcomctl32

if %ERRORLEVEL% equ 0 (
    echo Successfully built screen-off-no-sleep-%ARCH%.exe
) else (
    echo Error: Compilation failed for %ARCH%.
    exit /b %ERRORLEVEL%
)
exit /b 0
