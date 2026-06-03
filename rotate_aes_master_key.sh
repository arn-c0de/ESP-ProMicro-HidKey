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

# Replace the key via an atomic write (temp file in the same dir + os.replace),
# created 0600 so the new key is never briefly world-readable. The key value is
# never echoed to the terminal (avoids leaking it into scrollback/history/logs).
python3 - "$ENV_FILE" "$NEW_KEY" <<'PY'
import os
import re
import sys
import tempfile
from pathlib import Path

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

directory = env_path.parent
fd, tmp = tempfile.mkstemp(dir=directory, prefix=".env.", suffix=".tmp")
try:
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(updated)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, env_path)          # atomic within the same filesystem
    os.chmod(env_path, 0o600)          # tighten perms if the file pre-existed
except BaseException:
    os.unlink(tmp)
    raise
PY

echo "Rotated AES_MASTER_KEY in $ENV_FILE (value not printed)."

# Secrets in embedded_passwords.h are still encrypted with the OLD key until the
# header is regenerated. Rebuild it automatically when rotating the default .env.
if [ "$ENV_FILE" = "$SCRIPT_DIR/.env" ]; then
  echo "Regenerating embedded_passwords.h with the new key..."
  ( cd "$SCRIPT_DIR" && python3 generate_password_header.py )
  echo "Done. Re-flash the device with ./build.sh to apply."
else
  echo "Reminder: regenerate embedded_passwords.h and re-flash so secrets use the new key."
fi
