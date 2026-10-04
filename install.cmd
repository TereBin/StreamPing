@echo off
setlocal
set "pluginDll=%~dp0obs-plugins\64bit\streamping.dll"
set "localeFile=%~dp0data\obs-plugins\streamping\locale\ko-KR.ini"

if not exist "%pluginDll%" set "pluginDll=%~dp0build\RelWithDebInfo\streamping.dll"
if not exist "%localeFile%" set "localeFile=%~dp0data\locale\ko-KR.ini"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install-windows.ps1" -PluginDll "%pluginDll%" -LocaleFile "%localeFile%" -WaitForConfirmation %*
set "exitCode=%ERRORLEVEL%"
exit /b %exitCode%
