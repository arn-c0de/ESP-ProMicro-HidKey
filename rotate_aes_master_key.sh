#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
ENV_FILE="${1:-$SCRIPT_DIR/.env}"

if [ ! -f "$ENV_FILE" ]; then
  echo "Error: env file not found: $ENV_FILE" >&2
  exit 1
fi

if ! grep -q '^AES_MASTER_KEY=' "$ENV_FILE"; then
  echo "Error: AES_MASTER_KEY entry missing in $ENV_FILE" >&2
  exit 1
fi

NEW_KEY="$(python3 - <<'PY'
import secrets
print(secrets.token_hex(16).upper())
PY
)"

OLD_KEY="$(grep -E '^AES_MASTER_KEY=' "$ENV_FILE" | head -n1 | cut -d= -f2-)"

python3 - "$ENV_FILE" "$NEW_KEY" <<'PY'
from pathlib import Path
import re
import sys

env_path = Path(sys.argv[1])
new_key = sys.argv[2]
content = env_path.read_text(encoding="utf-8")
updated, count = re.subn(
    r"^AES_MASTER_KEY=.*$",
    f"AES_MASTER_KEY={new_key}",
    content,
    count=1,
    flags=re.MULTILINE,
)
if count != 1:
    raise SystemExit(f"Failed to replace AES_MASTER_KEY in {env_path}")
env_path.write_text(updated, encoding="utf-8")
PY

echo "Updated AES_MASTER_KEY in $ENV_FILE"
echo "Old: $OLD_KEY"
echo "New: $NEW_KEY"
