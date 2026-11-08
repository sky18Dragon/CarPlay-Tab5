#!/bin/sh
# Run locally or in CI. BASE_SHA checks the whole PR, including earlier commits.
set -eu
cd "$(dirname "$0")/.."
if [ -n "${BASE_SHA:-}" ] && ! printf '%s' "$BASE_SHA" | grep -Eq '^0+$'; then
    git diff --check "$BASE_SHA" HEAD
elif git rev-parse --verify HEAD^ >/dev/null 2>&1; then
    git diff --check HEAD^ HEAD
fi
git diff --cached --check
git diff --check
python3 - <<'PYTHON'
import ast
from pathlib import Path
import subprocess

paths = subprocess.check_output(['git', 'ls-files', '-z']).decode().split('\0')
for name in filter(None, paths):
    path = Path(name)
    if path.suffix == '.py':
        ast.parse(path.read_bytes(), filename=name)
    elif path.suffix == '.sh':
        first_line = path.read_text().splitlines()[0]
        shell = 'bash' if 'bash' in first_line else 'sh'
        subprocess.run([shell, '-n', name], check=True)
print('Tracked Python and shell scripts: syntax OK')
PYTHON
python3 scripts/configure_tab5_info.py --check
