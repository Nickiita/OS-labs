@echo off
setlocal

echo [1/2] Configuring project with CMake...
cmake -B build -G "MinGW Makefiles"
if %errorlevel% neq 0 (
    echo Configuration failed!
    exit /b %errorlevel%
)

echo [2/2] Building project...
cmake --build build
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)

echo Build completed successfully!
endlocal