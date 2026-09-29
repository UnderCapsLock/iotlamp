@echo off
setlocal enabledelayedexpansion
title LightPlus Health Check

rem Find tools\healthcheck.py: next to this file, one level up, or common project locations
set "SCRIPT="
for %%D in ("%~dp0" "%~dp0..\" "%USERPROFILE%\Downloads\iotlamp\" "%USERPROFILE%\Documents\iotlamp\" "C:\iotlamp\") do (
  if not defined SCRIPT if exist "%%~fD\tools\healthcheck.py" set "SCRIPT=%%~fD\tools\healthcheck.py"
)

if not defined SCRIPT (
  echo Could not find tools\healthcheck.py.
  echo Put this file inside the iotlamp project folder, or edit the location
  echo list inside this script and add your project path.
  echo.
  pause
  exit /b 1
)

set "PY=py"
where py >nul 2>nul || set "PY=python"
where %PY% >nul 2>nul || (
  echo Python is not installed or not on PATH.
  echo Install it from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
  echo.
  pause
  exit /b 1
)

echo Running: !SCRIPT:\\=\!
echo.
"%PY%" "!SCRIPT!" %*
echo.
echo Press any key to close...
pause >nul
