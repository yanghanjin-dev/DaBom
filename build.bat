@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not exist build mkdir build

set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)

if defined VCVARS (
  call "%VCVARS%"
  if errorlevel 1 (
    echo [ERROR] vcvars64 failed
    exit /b 1
  )
  echo [BUILD] dabom.exe ^(MSVC^) ...
  cl /nologo /W3 /O2 /utf-8 /DUNICODE /D_UNICODE /TC /I src src\main.c src\image.c src\files.c src\config.c src\actions.c src\ctxmenu.c /Fe:build\dabom.exe /Fo:build\ /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup user32.lib gdi32.lib comctl32.lib ole32.lib shell32.lib windowscodecs.lib uuid.lib comdlg32.lib dwmapi.lib advapi32.lib uxtheme.lib winspool.lib
  if errorlevel 1 (
    echo [FAIL] compile/link error
    exit /b 1
  )
  goto :ok
)

REM Fallback: MSYS2 UCRT64 gcc when MSVC is not installed
set "GCC="
if exist "C:\msys64\ucrt64\bin\gcc.exe" set "GCC=C:\msys64\ucrt64\bin\gcc.exe"
if not defined GCC if exist "C:\msys64\mingw64\bin\gcc.exe" set "GCC=C:\msys64\mingw64\bin\gcc.exe"
if not defined GCC (
  echo [ERROR] Neither MSVC ^(vcvars64^) nor MSYS2 gcc found.
  echo Install VS C++ desktop workload, or MSYS2 ucrt64-gcc.
  exit /b 1
)

for %%G in ("%GCC%") do set "GCCDIR=%%~dpG"
set "PATH=%GCCDIR%;%PATH%"
echo [BUILD] dabom.exe ^(gcc fallback^) ...
"%GCC%" -municode -DUNICODE -D_UNICODE -O2 -I src -o build\dabom.exe src\main.c src\image.c src\files.c src\config.c src\actions.c src\ctxmenu.c -mwindows -ldwmapi -lcomctl32 -lole32 -lshell32 -lwindowscodecs -luuid -lcomdlg32 -lgdi32 -luser32 -ladvapi32 -loleaut32 -luxtheme -lwinspool
if errorlevel 1 (
  echo [FAIL] gcc compile/link error
  exit /b 1
)

:ok
echo.
echo [OK] build\dabom.exe
endlocal
exit /b 0
