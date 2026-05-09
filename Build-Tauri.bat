@echo off
setlocal enabledelayedexpansion
for /f "delims=" %%I in ('node -e "const fs=require('fs'); process.stdout.write(fs.realpathSync(process.argv[1]))" "%~dp0."') do set "REPO_DIR=%%I"
if not defined REPO_DIR set "REPO_DIR=%~dp0"
cd /d "%REPO_DIR%"

set "INSTALLER_DIR=%cd%\installer"
set "NSIS_DIR=src-tauri\target\release\bundle\nsis"
set "MSI_DIR=src-tauri\target\release\bundle\msi"
set "RELEASE_EXE=%INSTALLER_DIR%\helltime-setup-x64.exe"
set "RELEASE_MSI=%INSTALLER_DIR%\helltime-installer-x64.msi"
set "CHECKSUMS=%INSTALLER_DIR%\SHA256SUMS.txt"

echo Building helltime (Tauri)...
call npm run tauri build -- --bundles nsis,msi
if errorlevel 1 (
  echo.
  echo Build failed.
  exit /b 1
)

if not exist "%INSTALLER_DIR%" mkdir "%INSTALLER_DIR%"

if exist "%RELEASE_EXE%" del /q "%RELEASE_EXE%"
if exist "%RELEASE_MSI%" del /q "%RELEASE_MSI%"
if exist "%CHECKSUMS%" del /q "%CHECKSUMS%"

powershell -NoProfile -Command ^
  "$exe = Get-ChildItem -Path '%NSIS_DIR%' -Filter '*_x64-setup.exe' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1; " ^
  "if (-not $exe) { throw 'NSIS installer not found.' }; " ^
  "$msi = Get-ChildItem -Path '%MSI_DIR%' -Filter '*.msi' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1; " ^
  "if (-not $msi) { throw 'MSI installer not found.' }; " ^
  "Copy-Item -Path $exe.FullName -Destination '%RELEASE_EXE%' -Force; " ^
  "Copy-Item -Path $msi.FullName -Destination '%RELEASE_MSI%' -Force; " ^
  "$hashAlgo = [System.Security.Cryptography.SHA256]::Create(); " ^
  "$files = @('%RELEASE_EXE%','%RELEASE_MSI%'); " ^
  "$lines = foreach ($file in $files) { " ^
  "  $stream = [System.IO.File]::OpenRead($file); " ^
  "  try { $hashBytes = $hashAlgo.ComputeHash($stream) } finally { $stream.Dispose() }; " ^
  "  $hash = ([System.BitConverter]::ToString($hashBytes)).Replace('-', '').ToLowerInvariant(); " ^
  "  '{0}  {1}' -f $hash, (Split-Path -Leaf $file) " ^
  "}; " ^
  "$lines | Set-Content -Path '%CHECKSUMS%'; " ^
  "Write-Host ('Copied ' + $exe.Name + ' to %RELEASE_EXE%'); " ^
  "Write-Host ('Copied ' + $msi.Name + ' to %RELEASE_MSI%')"
if errorlevel 1 (
  echo.
  echo Release asset collection failed.
  exit /b 1
)

echo.
echo Build finished.
echo Installer files are in:
echo   %INSTALLER_DIR%
endlocal
