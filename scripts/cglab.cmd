@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0cglab.ps1" %*
exit /b %ERRORLEVEL%
