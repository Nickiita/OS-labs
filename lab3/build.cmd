@echo off
setlocal

pushd "%~dp0" || exit /b 1
cmake -S . -B build -G "MinGW Makefiles" && cmake --build build
set "result=%errorlevel%"
popd
exit /b %result%
