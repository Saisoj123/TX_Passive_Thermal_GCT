@echo off
REM Build and upload script for TX Master ESP32
REM Usage: build_tx.bat [debug]

echo ========================================
echo  TX Passive Thermal GCT Build Script
echo ========================================

if "%1"=="debug" (
    echo Building DEBUG version...
    pio run -e tx-master-debug
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo Build successful! Upload now? [Y/N]
        set /p confirm=
        if /i "%confirm%"=="Y" (
            echo.
            echo Uploading to ESP32...
            pio run -e tx-master-debug --target upload
            echo Upload command completed with exit code: %ERRORLEVEL%
            if %ERRORLEVEL% EQU 0 (
                echo Upload successful!
            ) else (
                echo Upload FAILED! Check ESP32 connection and COM port.
            )
        ) else (
            echo Upload skipped.
        )
    ) else (
        echo.
        echo Build FAILED! Error code: %ERRORLEVEL%
        echo.
        echo Common issues:
        echo - PlatformIO not installed: Install PlatformIO Core or VS Code extension
        echo - ESP32 not connected: Check USB connection
        echo - Wrong COM port: Check device manager
        goto end
    )
) else (
    echo Building RELEASE version...
    pio run -e tx-master-esp32
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo Build successful! Upload now? [Y/N]
        set /p confirm=
        if /i "%confirm%"=="Y" (
            echo.
            echo Uploading to ESP32...
            pio run -e tx-master-esp32 --target upload
            echo Upload command completed with exit code: %ERRORLEVEL%
            if %ERRORLEVEL% EQU 0 (
                echo Upload successful!
            ) else (
                echo Upload FAILED! Check ESP32 connection and COM port.
            )
        ) else (
            echo Upload skipped.
        )
    ) else (
        echo.
        echo Build FAILED! Error code: %ERRORLEVEL%
        echo.
        echo Common issues:
        echo - PlatformIO not installed: Install PlatformIO Core or VS Code extension
        echo - ESP32 not connected: Check USB connection
        echo - Wrong COM port: Check device manager
        goto end
    )
)

echo.
echo Done!
:end
pause
