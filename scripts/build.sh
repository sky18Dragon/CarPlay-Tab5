#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v idf.py >/dev/null; then
  echo '先执行 source <ESP-IDF v5.5.3>/export.sh' >&2
  exit 1
fi
if [[ "$(idf.py --version)" != 'ESP-IDF v5.5.3' ]]; then
  echo '此基线要求 ESP-IDF v5.5.3' >&2
  exit 1
fi
idf.py -DIDF_TARGET=esp32p4 build
