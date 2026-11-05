#!/usr/bin/env bash
set -e

echo "[1/3] Preparing build directory..."
mkdir -p build
cd build

echo "[2/3] Configuring with CMake..."
cmake ..

echo "[3/3] Building project..."
cmake --build .

echo -e "\nRunning test utility"
./test_runner