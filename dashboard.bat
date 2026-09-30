@echo off
setlocal
title LightPlus Dashboard

rem Find the project folder (works from Desktop or anywhere)
set "TOOLS="
for %%D in ("%~dp0" "%~dp0..\" "%USERPROFILE%\Downloads\iotlamp\" "%USERPROFILE%\Documents\iotlamp\" "C:\iotlamp\") do (
  if not defined TOOLS if exist "%%~fD\tools\healthcheck.py" set "TOOLS=%%~fD\tools"
)
if not defined TOOLS (
  echo Could not find the LightPlus project folder.
  echo Put this file inside the iotlamp folder, or edit the path list in this script.
  pause
  exit /b 1
)

set "PY=py"
where py >nul 2>nul || set "PY=python"
where %PY% >nul 2>nul || (
  echo Python is not installed or not on PATH.
  pause
  exit /b 1
)

set "LAMP_IP="
"%PY%" "%TOOLS%\lamp_ip.py" > "%TEMP%\lamp_ip.txt" 2>nul
if exist "%TEMP%\lamp_ip.txt" set /p LAMP_IP=<"%TEMP%\lamp_ip.txt"
del "%TEMP%\lamp_ip.txt" >nul 2>nul

if not defined LAMP_IP (
  echo Could not find the lamp on the network.
  echo Make sure it has power, then run check.bat for details.
  pause
  exit /b 1
)

echo Opening http://%LAMP_IP%/ ...
start "" "http://%LAMP_IP%/"
