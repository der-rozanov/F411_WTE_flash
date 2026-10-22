@echo off
setlocal
rem ---------------------------------------------------------------------------
rem  Flash Matek F411-WTE over USB DFU (STM32 ROM system bootloader).
rem
rem  If our firmware is running, the script switches the board to DFU itself
rem  over USB CDC (enter_dfu.ps1). Otherwise enter DFU by hand: hold the
rem  button on the board (BOOT/DFU) while plugging USB-C, then release it.
rem
rem  Usage:  flash_dfu.bat [firmware.elf|.hex|.bin]
rem  Default: Debug\F411CEU6.elf next to this script.
rem ---------------------------------------------------------------------------

cd /d "%~dp0"

set "FW=%~1"
if "%FW%"=="" set "FW=Debug\F411CEU6.elf"

if not exist "%FW%" (
    echo [ERR] Firmware not found: %FW%
    echo       Build the project in CubeIDE first ^(Ctrl+B^).
    goto :fail
)

rem --- locate STM32CubeProgrammer CLI ----------------------------------------
set "CLI=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if exist "%CLI%" goto :have_cli
for /d %%D in ("C:\ST\STM32CubeIDE_*") do (
    for /d %%P in ("%%D\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*") do (
        if exist "%%P\tools\bin\STM32_Programmer_CLI.exe" set "CLI=%%P\tools\bin\STM32_Programmer_CLI.exe"
    )
)
if not exist "%CLI%" (
    echo [ERR] STM32_Programmer_CLI.exe not found. Install STM32CubeProgrammer.
    goto :fail
)
:have_cli

rem --- ask running firmware to jump to DFU (USB CDC, 1200 baud touch) --------
"%CLI%" -l usb 2>nul | findstr /c:"Device Index" >nul && goto :found
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0enter_dfu.ps1"

rem --- wait for the DFU device (about 20 s) ----------------------------------
echo Waiting for DFU device (if nothing happens: hold the board button and replug USB)...
set /a TRIES=0
:wait
"%CLI%" -l usb 2>nul | findstr /c:"Device Index" >nul && goto :found
set /a TRIES+=1
if %TRIES% geq 20 goto :notfound
ping -n 2 127.0.0.1 >nul
goto :wait

:notfound
echo [ERR] No DFU device found.
echo       Check the USB cable (must carry data) and that Device Manager shows
echo       "STM32 BOOTLOADER" (VID 0483, PID DF11).
goto :fail

:found
echo Flashing %FW% ...
set "ADDR="
if /i "%~x1"==".bin" set "ADDR=0x08000000"
"%CLI%" -c port=USB1 -w "%FW%" %ADDR% -v -s 0x08000000
if errorlevel 1 goto :fail

echo.
echo [OK] Done, firmware is running.
exit /b 0

:fail
echo.
if "%NOPAUSE%"=="" pause
exit /b 1
