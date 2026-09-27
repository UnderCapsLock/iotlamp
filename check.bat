@echo off
setlocal
title LightPlus Health Check
cd /d "%~dp0"

set "PY=py"
where py >nul 2>nul || set "PY=python"
where %PY% >nul 2>nul || (
  echo Python is not installed or not on PATH.
  echo Install it from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
  echo.
  pause
  exit /b 1
)

"%PY%" "%~dp0tools\healthcheck.py" %*
echo.
echo Press any key to close...
pause >nul
