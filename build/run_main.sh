#!/bin/bash

# Exit immediately if a command fails
set -e

# Run CMake configuration
cmake ..

# Build with 4 parallel jobs
cmake --build . -j4

# Run the compiled application
./MyQtQuickApp
