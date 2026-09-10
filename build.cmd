@echo off
setlocal
set "ROOT=%~dp0"
set "VCVARS="

if defined VCVARSALL set "VCVARS=%VCVARSALL%"
if defined VCVARS goto found

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
if defined VCVARS goto found

if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat"

:found
if not defined VCVARS (
  echo Microsoft C++ build tools were not found.
  exit /b 1
)

if defined Q2_PREP_TOOLSET (
  call "%VCVARS%" x86 -vcvars_ver=%Q2_PREP_TOOLSET%
) else (
  call "%VCVARS%" x86
)
if errorlevel 1 exit /b 1

pushd "%ROOT%"

set "COPIED_WINCD=0"
if not exist "wincd.dll" (
  if exist "..\wincd.dll" (
    copy /y "..\wincd.dll" "wincd.dll" >nul
    if errorlevel 1 goto failed
    set "COPIED_WINCD=1"
  ) else (
    echo Missing wincd.dll. Place it next to build.cmd, or build it with _analysis\build.cmd
    goto failed
  )
)

rc.exe /nologo /foQ2Win95Prep.res prep_resources.rc
if errorlevel 1 goto failed

set "SRC=main.cpp prep_core.cpp prep_io.cpp prep_audio.cpp prep_copy.cpp"
set "CFLAGS=/nologo /std:c++14 /EHsc /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /MT /O2 /W3"
set "LIBS=user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib"

cl.exe %CFLAGS% %SRC% Q2Win95Prep.res /link /OUT:Q2Win95Prep.exe /SUBSYSTEM:WINDOWS,6.01 /MACHINE:X86 %LIBS%
if errorlevel 1 goto failed
cl.exe %CFLAGS% tests.cpp prep_core.cpp prep_io.cpp prep_audio.cpp prep_copy.cpp Q2Win95Prep.res /link /OUT:Q2Win95PrepTests.exe /SUBSYSTEM:CONSOLE,6.01 /MACHINE:X86 advapi32.lib
if errorlevel 1 goto failed
Q2Win95PrepTests.exe
if errorlevel 1 goto failed
del /q Q2Win95Prep.res main.obj prep_core.obj prep_io.obj prep_audio.obj prep_copy.obj tests.obj 2>nul
if "%COPIED_WINCD%"=="1" del /q wincd.dll 2>nul
popd
echo Build and self-tests completed successfully.
exit /b 0

:failed
del /q Q2Win95Prep.res main.obj prep_core.obj prep_io.obj prep_audio.obj prep_copy.obj tests.obj 2>nul
if "%COPIED_WINCD%"=="1" del /q wincd.dll 2>nul
popd
exit /b 1
