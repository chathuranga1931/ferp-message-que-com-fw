@echo off
REM run.win.bat — FERP Device Tool launcher for Windows

setlocal EnableDelayedExpansion

set VENV_DIR=.env
set SCRIPT_DIR=%~dp0

cd /d "%SCRIPT_DIR%"

REM ── 0. Sync messages-json from repo source (if available) ──────────────────
set SRC_MSGS=..\..\src\app-messages\messages
if exist "%SRC_MSGS%\" (
    echo Syncing messages-json from repo source...
    if exist "messages-json\" rmdir /s /q messages-json
    robocopy "%SRC_MSGS%" "messages-json" /e /mir /njh /njs /ndl /nc /ns /np > nul
    if errorlevel 8 (
        echo WARNING: Failed to sync messages-json from repo source.
    ) else (
        echo messages-json updated.
    )
) else (
    echo NOTE: Repo source messages not found -- running with existing messages-json.
    echo       The message definitions may not be up to date.
    echo       ^(Expected: %SCRIPT_DIR%%SRC_MSGS%^)
)

REM ── 1. Find python3.13.exe ─────────────────────────────────────────────────
set PYTHON=
where python3.13.exe >nul 2>&1
if not errorlevel 1 set PYTHON=python3.13.exe

if "!PYTHON!"=="" (
    echo ERROR: python3.13.exe not found on this system.
    echo Please install Python 3.13 from https://www.python.org/downloads/
    echo   ^• Or via winget:  winget install Python.Python.3.13
    pause
    exit /b 1
)

echo Python 3.13 found.

REM ── 3. Create virtual environment if needed ─────────────────────────────────
if not exist "%VENV_DIR%\Scripts\activate.bat" (
    echo Creating virtual environment...
    !PYTHON! -m venv %VENV_DIR%
)

REM ── 4. Install / update dependencies ────────────────────────────────────────
echo Installing requirements...
%VENV_DIR%\Scripts\pip.exe install --quiet --upgrade pip
%VENV_DIR%\Scripts\pip.exe install --quiet -r requirements.txt

REM ── 5. Run the tool ──────────────────────────────────────────────────────────
echo Starting FERP Device Tool...
%VENV_DIR%\Scripts\python.exe main.py

endlocal
