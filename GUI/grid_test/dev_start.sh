#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# Build
cmake -S . -B build -G Ninja
cmake --build build

# Run (dev windowed by default)
./build/grid_test --dev