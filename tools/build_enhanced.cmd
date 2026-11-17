@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64_x86
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0..\engine"
if errorlevel 1 exit /b %errorlevel%
set WAFLOCK=.lock-waf-local-20260927
if not exist ".lock-waf-local-20260927" (
  echo Existing local Waf configuration is missing. Use the original configure driver first.
  exit /b 1
)
py -3.14 waf build -j8 --targets=xash,vgui,ref_gl,menu
exit /b %errorlevel%
