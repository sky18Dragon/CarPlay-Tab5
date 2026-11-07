#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Source ESP-IDF v5.5.3/export.sh first}"
cmake -S tests/crypto -B artifacts/crypto-build -DIDF_MBEDTLS="$IDF_PATH/components/mbedtls/mbedtls"
cmake --build artifacts/crypto-build --target test_crypto -j 6
./artifacts/crypto-build/test_crypto
