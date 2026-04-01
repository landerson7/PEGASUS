#!/bin/bash
set -e

rm -rf build
cmake -S . -B build -G Ninja
cmake --build build
cd ./build
./hud --dev --dummy --small-display
echo $?
