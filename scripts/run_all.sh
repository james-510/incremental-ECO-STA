#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if command -v cmake >/dev/null 2>&1; then
    cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-cmake --parallel 4
    ctest --test-dir build-cmake --output-on-failure
else
    make -j4
    make test
fi
