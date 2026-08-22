@echo off
:: build.win.bat — Windows wrapper for build.py
::
:: Sets Windows-specific defaults for the serial port and IDF path, then
:: delegates all work to build.py.
::
:: Usage:
::   build.win.bat [--serial-port COMx] <action>
::
:: Actions:
::   --cleanall       Remove build directory only
::   --buildall       Build app + SPIFFS
::   --buildapp       Build app binary only
::   --flashall       Build + flash all (app + SPIFFS)
::   --flashspiffs    Build + flash SPIFFS partition only
::   --flashapp       Build + flash app partition only
::   --console        Open serial monitor
::   --release        Full release workflow
::   --create-bundle  Create OTA bundle from existing build artifacts
::
:: Override the serial port at runtime:
::   build.win.bat --serial-port COM5 --flashapp
::
:: Common IDF install locations on Windows:
::   C:\Espressif\frameworks\esp-idf-v6.0.1   (official installer)
::   %USERPROFILE%\.espressif\v6.0.1\esp-idf  (manual install)

:: ── Windows defaults (edit as needed) ────────────────────────────────────────
set IDF_PATH=D:\esp\v6.0.1\esp-idf
set DEFAULT_PORT=COM3

:: Capture the script's own directory before any "shift" runs — plain "shift"
:: (no /1) shifts %0 too, so if this were read after the arg-parsing loop it
:: would end up pointing at the last CLI argument instead of this file.
set SCRIPT_DIR=%~dp0

:: ── Activate IDF environment so idf.py is in PATH before build.py runs ───────
if not exist "%IDF_PATH%\export.bat" (
    echo ERROR: IDF not found at %IDF_PATH%
    echo        Update IDF_PATH at the top of this file.
    exit /b 1
)
call "%IDF_PATH%\export.bat" > NUL
if errorlevel 1 (
    echo ERROR: Failed to activate IDF environment.
    echo        Try running: %IDF_PATH%\export.bat
    exit /b 1
)

:: ── Parse --serial-port override; forward everything else to build.py ────────
set SERIAL_PORT=%DEFAULT_PORT%
set PASS_ARGS=

:parse_args
if "%~1"=="" goto run
if /i "%~1"=="--serial-port" (
    set SERIAL_PORT=%~2
    shift
    shift
    goto parse_args
)
if defined PASS_ARGS (
    set PASS_ARGS=%PASS_ARGS% %~1
) else (
    set PASS_ARGS=%~1
)
shift
goto parse_args

:run
python "%SCRIPT_DIR%build.py" --serial-port %SERIAL_PORT% --idf-path "%IDF_PATH%" %PASS_ARGS%
