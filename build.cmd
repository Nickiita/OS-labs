@echo off
setlocal

echo [1/3] Updating repository from Git...
git pull

echo [2/3] Configuring with CMake (MinGW)...
if not exist build mkdir build
cd build
cmake -G "MinGW Makefiles" ..

echo [3/3] Building project...
cmake --build .

echo Done!
endlocal