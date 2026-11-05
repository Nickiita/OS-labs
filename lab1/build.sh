#!/usr/bin/env bash
set -e

echo "[1/3] Updating repository from Git..."
git pull

echo "[2/3] Configuring with CMake..."
mkdir -p build
cd build
cmake ..

echo "[3/3] Building project..."
cmake --build .

echo -e "\nRunning Hello World!"
./main