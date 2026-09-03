@echo off
REM Diagnostic launcher — shows a console so startup errors are visible.
REM For normal use (no terminal window), double-click CrashVault.vbs instead.
setlocal
set "WIN_DIR=%~dp0"
for /f "delims=" %%i in ('wsl wslpath -a "%WIN_DIR:~0,-1%"') do set "WSL_DIR=%%i"
if not defined WSL_DIR (
  echo Failed to resolve WSL path for: %WIN_DIR%
  echo Is WSL installed and running?
  pause
  exit /b 1
)
echo Launching CrashVault from: %WSL_DIR%
wsl.exe -e bash -lc "chmod +x '%WSL_DIR%/launch.sh' 2>/dev/null; exec '%WSL_DIR%/launch.sh'"
if errorlevel 1 (
  echo.
  echo CrashVault failed to start. See messages above.
  pause
)
