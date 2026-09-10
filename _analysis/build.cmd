@echo off
setlocal
set "HERE=%~dp0"
set "ROOT=%HERE%.."
if not defined WATCOM set "WATCOM=C:\WATCOM"
set "WCC=%WATCOM%\BINNT64\wcc386.exe"
set "WLINK=%WATCOM%\BINNT64\wlink.exe"
if not exist "%WCC%" set "WCC=%WATCOM%\BINNT\wcc386.exe"
if not exist "%WLINK%" set "WLINK=%WATCOM%\BINNT\wlink.exe"
if not exist "%WCC%" (
  echo Open Watcom wcc386.exe was not found. Set WATCOM.
  exit /b 1
)

set "INCLUDE=%WATCOM%\H;%WATCOM%\H\NT;%WATCOM%\H\NT\DIRECTX"
pushd "%HERE%"

echo Building wincd.dll
"%WCC%" -bt=nt -s -ox -zq -w3 -fo=q2cd_winmm.obj q2cd_winmm.c
if errorlevel 1 goto failed
"%WLINK%" @q2cd_winmm.lnk name wincd.dll file q2cd_winmm.obj
if errorlevel 1 goto failed

copy /y wincd.dll "%ROOT%\wincd.dll" >nul
if errorlevel 1 goto failed

python pe_win95_stamp.py "%ROOT%\wincd.dll"
if errorlevel 1 goto failed
python patch_wincd.py
if errorlevel 1 goto failed

del /q q2cd_winmm.obj 2>nul
popd
echo Build OK
exit /b 0

:failed
popd
echo Build FAILED
exit /b 1
