#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cmake -S tests/ui -B artifacts/ui-build
cmake --build artifacts/ui-build -j 6
./artifacts/ui-build/render
