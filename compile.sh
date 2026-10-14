#!/usr/bin/env sh
# Convenience wrapper around the desktop CMake build. See README.md.
set -e

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
