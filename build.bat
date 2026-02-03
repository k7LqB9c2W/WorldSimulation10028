@echo off
setlocal enabledelayedexpansion

rem ====== Config ======
set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
set "SFML=C:\SFML-2.6.1"
set "OUT=WorldSimulation.exe"
set "SRC=main.cpp country.cpp map.cpp renderer.cpp news.cpp technology.cpp culture.cpp great_people.cpp resource.cpp trade.cpp economy.cpp"

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
if not exist "%SFML%\lib" (
  echo Could not find SFML lib at:
  echo   %SFML%\lib
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
call "%VSDEVCMD%" -arch=x64 || exit /b 1
cl /nologo /EHsc /std:c++17 ^
  /I "%SFML%\include" ^
  %SRC% ^
  /Fe:%OUT% ^
  /MD ^
  /link /LIBPATH:"%SFML%\lib" ^
  sfml-graphics.lib sfml-window.lib sfml-system.lib sfml-audio.lib
exit /b %ERRORLEVEL%

:debug
echo.
echo === Building Debug x64 ===
call "%VSDEVCMD%" -arch=x64 || exit /b 1
cl /nologo /EHsc /std:c++17 ^
  /I "%SFML%\include" ^
  %SRC% ^
  /Fe:%OUT% ^
  /MDd /Zi /Od ^
  /link /LIBPATH:"%SFML%\lib" ^
  sfml-graphics-d.lib sfml-window-d.lib sfml-system-d.lib sfml-audio-d.lib
exit /b %ERRORLEVEL%

:run
echo.
echo === Build then Run (Release) ===
call "%~f0" release || exit /b 1
echo.
echo === Launching %OUT% ===
"%CD%\%OUT%"
exit /b %ERRORLEVEL%

:clean
echo.
echo === Cleaning build artifacts ===
del /q *.obj 2>nul
del /q *.pdb 2>nul
del /q *.ilk 2>nul
del /q "%OUT%" 2>nul
echo Done.
exit /b 0

:usage
echo.
echo Usage:
echo   build.bat release   Build release with MSVC and SFML
echo   build.bat debug     Build debug with MSVC and SFML
echo   build.bat run       Build release then run the app
echo   build.bat clean     Delete build outputs
echo.
echo Edit VSDEVCMD and SFML variables at the top if your paths differ.
exit /b 1
