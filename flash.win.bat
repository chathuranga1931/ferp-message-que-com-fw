@echo off
:: flash.win.bat — Windows wrapper for flash.py
::
:: Sets Windows-specific defaults for the serial port and IDF path, then
:: delegates all work to flash.py.
::
:: Usage:
::   flash.win.bat [--serial-port COMx] <action>
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
::   flash.win.bat --serial-port COM5 --flashapp
::
:: Common IDF install locations on Windows:
::   C:\Espressif\frameworks\esp-idf-v6.0.1   (official installer)
::   %USERPROFILE%\.espressif\v6.0.1\esp-idf  (manual install)

:: ── Windows defaults (edit as needed) ────────────────────────────────────────
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v6.0.1
set DEFAULT_PORT=COM3

:: ── Parse --serial-port override; forward everything else to flash.py ────────
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
set SCRIPT_DIR=%~dp0
python "%SCRIPT_DIR%flash.py" --serial-port %SERIAL_PORT% --idf-path "%IDF_PATH%" %PASS_ARGS%
