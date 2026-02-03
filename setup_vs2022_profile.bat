@echo off
setlocal

REM Builds an optimized + symbols configuration suitable for Visual Studio's Performance Profiler.
REM Requires: Visual Studio 2022 (x64 Native Tools prompt recommended) + vcpkg at C:\vcpkg (or set VCPKG_ROOT).

if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\vcpkg"

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  echo ERROR: vcpkg.exe not found at "%VCPKG_ROOT%\vcpkg.exe"
  echo        Set VCPKG_ROOT to your vcpkg folder, or install vcpkg to C:\vcpkg.
  exit /b 1
)

echo Installing SFML via vcpkg...
"%VCPKG_ROOT%\vcpkg.exe" install sfml:x64-windows
if errorlevel 1 exit /b 1

echo Cleaning build folder...
if exist "out\build\vs2022" rmdir /s /q "out\build\vs2022"

echo Configuring (VS 2022 x64)...
cmake -S . -B out\build\vs2022 -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DSFML_DIR="%VCPKG_ROOT%\installed\x64-windows\share\sfml"
if errorlevel 1 exit /b 1

echo Building RelWithDebInfo (optimized + PDBs)...
cmake --build out\build\vs2022 --config RelWithDebInfo
if errorlevel 1 exit /b 1

echo.
echo Done.
echo - Open: out\build\vs2022\CountrySimulator.sln
echo - In VS: Debug ^> Performance Profiler... ^> CPU Usage ^> Start
echo - Tip: Set Debugging Working Directory to the repo root so assets load.
endlocal
