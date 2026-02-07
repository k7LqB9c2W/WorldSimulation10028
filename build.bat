@echo off
setlocal enabledelayedexpansion

rem ====== Config ======
set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
set "SFML=C:\SFML-2.6.1"
set "GENERATOR=Ninja"
set "BUILD_ROOT=out\cmake"
set "TARGET=WorldSimulation"
set "CFG_RELEASE=Release"
set "CFG_DEBUG=Debug"
set "CMAKE_EXE=cmake"
set "NINJA_EXE=ninja"

rem ====== Help ======
if "%~1"=="" goto :usage
if "%~1"=="/?" goto :usage
if /i "%~1"=="help" goto :usage

rem ====== Ensure VS tools ======
if not exist "%VSDEVCMD%" (
  echo Could not find VsDevCmd.bat at:
  echo   %VSDEVCMD%
  echo Update VSDEVCMD in this script.
  exit /b 1
)

rem ====== Ensure SFML ======
if not exist "%SFML%\include" (
  echo Could not find SFML include at:
  echo   %SFML%\include
  echo Update SFML path in this script.
  exit /b 1
)
if not exist "%SFML%\lib\cmake\SFML\SFMLConfig.cmake" (
  echo Could not find SFML CMake package at:
  echo   %SFML%\lib\cmake\SFML\SFMLConfig.cmake
  echo Update SFML path in this script.
  exit /b 1
)

rem ====== Commands ======
if /i "%~1"=="release" goto :release
if /i "%~1"=="debug"   goto :debug
if /i "%~1"=="run"     goto :run
if /i "%~1"=="clean"   goto :clean

echo Unknown command: %~1
goto :usage

:release
echo.
echo === Building Release x64 ===
call :prepare_tools || exit /b 1
set "BUILD_DIR=%BUILD_ROOT%\release"
call :configure %CFG_RELEASE% "%BUILD_DIR%" || exit /b 1
call :build "%BUILD_DIR%" || exit /b 1
exit /b %ERRORLEVEL%

:debug
echo.
echo === Building Debug x64 ===
call :prepare_tools || exit /b 1
set "BUILD_DIR=%BUILD_ROOT%\debug"
call :configure %CFG_DEBUG% "%BUILD_DIR%" || exit /b 1
call :build "%BUILD_DIR%" || exit /b 1
exit /b %ERRORLEVEL%

:run
echo.
echo === Build then Run (Release) ===
call "%~f0" release || exit /b 1
echo.
set "EXE=%BUILD_ROOT%\release\bin\%TARGET%.exe"
if not exist "%EXE%" (
  set "EXE=%BUILD_ROOT%\release\%TARGET%.exe"
)
if not exist "%EXE%" (
  echo Could not find built executable:
  echo   %BUILD_ROOT%\release\bin\%TARGET%.exe
  exit /b 1
)
echo === Launching %EXE% ===
"%EXE%"
exit /b %ERRORLEVEL%

:clean
echo.
echo === Cleaning CMake/Ninja build artifacts ===
if exist "%BUILD_ROOT%" rmdir /s /q "%BUILD_ROOT%"
echo Done.
exit /b 0

:prepare_tools
call "%VSDEVCMD%" -arch=x64 || exit /b 1

where cmake >nul 2>nul
if errorlevel 1 (
  set "CMAKE_EXE=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if /i not "%CMAKE_EXE%"=="cmake" (
  if not exist "%CMAKE_EXE%" (
    echo Could not find cmake in PATH or at:
    echo   %CMAKE_EXE%
    exit /b 1
  )
)

where ninja >nul 2>nul
if errorlevel 1 (
  set "NINJA_EXE=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)
if /i not "%NINJA_EXE%"=="ninja" (
  if not exist "%NINJA_EXE%" (
    echo Could not find ninja in PATH or at:
    echo   %NINJA_EXE%
    exit /b 1
  )
)
exit /b 0

:configure
set "CFG=%~1"
set "BDIR=%~2"
echo.
echo === Configuring %CFG% x64 (%GENERATOR%) ===
"%CMAKE_EXE%" -S . -B "%BDIR%" -G "%GENERATOR%" ^
  -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
  -DCMAKE_BUILD_TYPE=%CFG% ^
  -DSFML_ROOT="%SFML%" ^
  -DSFML_DIR="%SFML%\lib\cmake\SFML"
exit /b %ERRORLEVEL%

:build
set "BDIR=%~1"
echo.
echo === Building in %BDIR% ===
"%CMAKE_EXE%" --build "%BDIR%" --parallel
exit /b %ERRORLEVEL%

:usage
echo.
echo Usage:
echo   build.bat release   Configure + build Release using CMake + Ninja
echo   build.bat debug     Configure + build Debug using CMake + Ninja
echo   build.bat run       Build release then run the app
echo   build.bat clean     Delete CMake/Ninja build outputs
echo.
echo Edit VSDEVCMD and SFML variables at the top if your paths differ.
exit /b 1
