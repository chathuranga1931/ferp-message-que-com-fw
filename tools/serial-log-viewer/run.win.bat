@echo off
:: run.win.bat — FERP Serial Log Viewer launcher for Windows

setlocal enabledelayedexpansion

set REQUIRED_MAJOR=3
set REQUIRED_MINOR=10
set VENV_DIR=.env
set SCRIPT_DIR=%~dp0

cd /d "%SCRIPT_DIR%"

:: ── 1. Find Python ────────────────────────────────────────────────────────────
set PYTHON=
for %%c in (python3.13 python3.12 python3.11 python3.10 python3 python) do (
    if "!PYTHON!"=="" (
        where %%c >NUL 2>&1
        if not errorlevel 1 set PYTHON=%%c
    )
)

if "!PYTHON!"=="" (
    echo ERROR: Python not found on this system.
    echo Please install Python 3.10 or above from https://www.python.org/downloads/
    exit /b 1
)

:: ── 2. Check version ──────────────────────────────────────────────────────────
for /f %%v in ('!PYTHON! -c "import sys; print(sys.version_info.major)"') do set PYTHON_MAJOR=%%v
for /f %%v in ('!PYTHON! -c "import sys; print(sys.version_info.minor)"') do set PYTHON_MINOR=%%v
for /f %%v in ('!PYTHON! -c "import sys; print(f\"{sys.version_info.major}.{sys.version_info.minor}\")"') do set PYTHON_VERSION=%%v

if !PYTHON_MAJOR! LSS %REQUIRED_MAJOR% goto version_error
if !PYTHON_MAJOR! EQU %REQUIRED_MAJOR% if !PYTHON_MINOR! LSS %REQUIRED_MINOR% goto version_error
goto version_ok

:version_error
echo ERROR: Python !PYTHON_VERSION! found, but Python 3.10 or above is required.
echo.
echo Please upgrade Python:
echo   - Download from https://www.python.org/downloads/
exit /b 1

:version_ok
echo Python !PYTHON_VERSION! found.

:: ── 3. Verify tkinter is available ───────────────────────────────────────────
!PYTHON! -c "import tkinter" >NUL 2>&1
if errorlevel 1 (
    echo.
    echo ERROR: tkinter is not available in the selected Python.
    echo.
    echo On Windows, tkinter is included with the standard python.org installer.
    echo Re-install Python from https://www.python.org/downloads/ and ensure
    echo "tcl/tk and IDLE" is checked during installation.
    exit /b 1
)

:: ── 4. Create / validate virtual environment ──────────────────────────────────
if exist "%VENV_DIR%" (
    "%VENV_DIR%\Scripts\python.exe" -c "" >NUL 2>&1
    if errorlevel 1 (
        echo Virtual environment is stale -- recreating...
        rmdir /s /q "%VENV_DIR%"
    )
)

if not exist "%VENV_DIR%" (
    echo Creating virtual environment...
    !PYTHON! -m venv "%VENV_DIR%"
)

:: ── 5. Install / update dependencies ─────────────────────────────────────────
echo Installing requirements...
"%VENV_DIR%\Scripts\pip.exe" install --quiet --upgrade pip
"%VENV_DIR%\Scripts\pip.exe" install --quiet -r requirements.txt

:: ── 6. Launch the viewer ──────────────────────────────────────────────────────
echo Starting FERP Serial Log Viewer...
"%VENV_DIR%\Scripts\python.exe" serial_log_viewer.py %*

endlocal
