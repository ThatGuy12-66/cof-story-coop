@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64_x86 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0.."
cl /nologo /W4 /WX /TC tests\story_transition_state_test.c /Fe:build-local-20260927\story_transition_state_test.exe /Fo:build-local-20260927\story_transition_state_test.obj
if errorlevel 1 exit /b %errorlevel%
build-local-20260927\story_transition_state_test.exe
exit /b %errorlevel%
