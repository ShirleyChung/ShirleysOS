@echo off
setlocal enabledelayedexpansion

REM ShirleyOS Windows QEMU Launcher
REM Allows running ShirleyOS directly on Windows (Command Prompt / PowerShell)

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=x86_64"

echo ========================================================
echo   ShirleyOS Windows Runner
echo   Target: %TARGET%
echo ========================================================

REM Find bash from Git or MSYS
set "BASH_BIN="
if exist "C:\Program Files\Git\bin\bash.exe" set "BASH_BIN=C:\Program Files\Git\bin\bash.exe"
if exist "C:\Program Files (x86)\Git\bin\bash.exe" set "BASH_BIN=C:\Program Files (x86)\Git\bin\bash.exe"
if "%BASH_BIN%"=="" (
    for /f "tokens=*" %%i in ('where bash 2^>nul') do (
        set "BASH_BIN=%%i"
        goto :found_bash
    )
)
:found_bash

if not "%BASH_BIN%"=="" (
    "%BASH_BIN%" ./run %*
    goto :eof
)

echo [Error] Git Bash is required to run the build & emulator scripts on Windows.
echo Please install Git for Windows (https://git-scm.com/download/win).
pause
