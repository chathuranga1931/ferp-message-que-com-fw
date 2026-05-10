@echo off
REM run.win.bat — FERP Device Tool launcher for Windows

setlocal EnableDelayedExpansion

set REQUIRED_MAJOR=3
set REQUIRED_MINOR=13
set VENV_DIR=.env
set SCRIPT_DIR=%~dp0

cd /d "%SCRIPT_DIR%"

REM ── 1. Find python ─────────────────────────────────────────────────────────
set PYTHON=
for %%c in (python3.13 python3 python) do (
    if "!PYTHON!"=="" (
        where %%c >nul 2>&1
        if not errorlevel 1 set PYTHON=%%c
    )
)

if "!PYTHON!"=="" (
    echo ERROR: Python not found on this system.
    echo Please install Python 3.13 or above from https://www.python.org/downloads/
    pause
    exit /b 1
)

REM ── 2. Check version ────────────────────────────────────────────────────────
for /f "delims=" %%v in ('!PYTHON! -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"') do set PYTHON_VERSION=%%v
for /f %%m in ('!PYTHON! -c "import sys; print(sys.version_info.major)"') do set PYTHON_MAJOR=%%m
for /f %%n in ('!PYTHON! -c "import sys; print(sys.version_info.minor)"') do set PYTHON_MINOR=%%n

if !PYTHON_MAJOR! LSS %REQUIRED_MAJOR% goto version_error
if !PYTHON_MAJOR! EQU %REQUIRED_MAJOR% if !PYTHON_MINOR! LSS %REQUIRED_MINOR% goto version_error
goto version_ok

:version_error
echo ERROR: Python !PYTHON_VERSION! is installed, but Python 3.13 or above is required.
echo.
echo Please upgrade Python:
echo   ^• Download from https://www.python.org/downloads/
echo   ^• Or via winget:  winget install Python.Python.3.13
pause
exit /b 1

:version_ok
echo Python !PYTHON_VERSION! found.

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
