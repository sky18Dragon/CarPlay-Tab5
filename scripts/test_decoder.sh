#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cmake -S tests/decoder -B artifacts/decoder-build -DCMAKE_BUILD_TYPE=Debug
cmake --build artifacts/decoder-build -j 6
./artifacts/decoder-build/test_decoder tests/assets/high_fixture.bin tests/assets/high_ltr.bin
