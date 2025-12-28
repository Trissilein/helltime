@echo off
setlocal
cd /d "%~dp0"
echo Building helltime (Tauri)...
npm run tauri build
if errorlevel 1 (
  echo.
  echo Build failed.
  pause
  exit /b 1
)
echo.
echo Build finished.
pause
endlocal
