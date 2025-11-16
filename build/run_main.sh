#!/bin/bash

# Exit immediately if a command fails
set -e

# Go into the build directory (create it if needed)
mkdir -p build
cd build

# Run CMake configuration
cmake ..

# Build with 4 parallel jobs
cmake --build . -j4

# Run the compiled application
./MyQtQuickApp
