#!/usr/bin/env bash
# Setup script for Secure HID Pro Micro
# - Creates/uses local Python venv at .venv
# - Installs Python requirements
# - Optionally installs Arduino CLI and sets up SparkFun cores + AESLib
# - Optionally copies .env.template -> .env (not filled)

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$PROJECT_DIR/.venv"
PYTHON_BIN="$VENV_DIR/bin/python"
PIP_BIN="$VENV_DIR/bin/pip"
INSTALL_ARDUINO_CLI=false
COPY_ENV=false
FORCE=false

usage() {
  cat <<EOF
Usage: setup.sh [options]
Options:
  -y, --yes                 Non-interactive (assume yes for prompts)
  --install-arduino         Install arduino-cli automatically
  --copy-env                Copy .env.template to .env if missing
  -h, --help                Show this help

Examples:
  ./setup.sh --install-arduino --copy-env
EOF
  exit 1
}

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes) FORCE=true; shift ;;
    --install-arduino) INSTALL_ARDUINO_CLI=true; shift ;;
    --copy-env) COPY_ENV=true; shift ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1"; usage ;;
  esac
done

# Utility: prompt yes/no
prompt_yes() {
  if [ "$FORCE" = true ]; then
    return 0
  fi
  local prompt="$1"; shift
  read -rp "$prompt (y/N): " ans
  case "$ans" in
    [Yy]* ) return 0 ;;
    * ) return 1 ;;
  esac
}

# 1) Check prerequisites
echo "\n🔧 Checking prerequisites..."
command -v python3 >/dev/null 2>&1 || { echo "[ERROR] python3 not found. Install Python 3 and re-run."; exit 1; }
command -v pip3 >/dev/null 2>&1 || { echo "[ERROR] pip3 not found. Install pip and re-run."; exit 1; }

# 2) Create virtualenv if missing
if [ ! -d "$VENV_DIR" ]; then
  echo "\n✅ Creating Python virtualenv at $VENV_DIR"
  python3 -m venv "$VENV_DIR"
else
  echo "\n✅ Virtualenv already exists at $VENV_DIR"
fi

# Ensure pip is up-to-date
echo "\n🔁 Upgrading pip inside venv..."
"$PIP_BIN" install --upgrade pip setuptools >/dev/null

# 3) Install Python requirements
if [ -f "$PROJECT_DIR/requirements.txt" ]; then
  echo "\n📦 Installing Python requirements from requirements.txt"
  "$PIP_BIN" install -r "$PROJECT_DIR/requirements.txt"
else
  echo "[WARN] requirements.txt not found, installing pycryptodome by default"
  "$PIP_BIN" install pycryptodome
fi

# 4) Optionally install arduino-cli
# Prefer system arduino-cli, but also handle local installs in $PROJECT_DIR/bin
if command -v arduino-cli >/dev/null 2>&1; then
  echo "\n✅ arduino-cli already installed: $(command -v arduino-cli)"
else
  # If a previous run installed it into the project bin, prefer that
  if [ -x "$PROJECT_DIR/bin/arduino-cli" ]; then
    echo "\nℹ arduino-cli found in project bin: $PROJECT_DIR/bin/arduino-cli"
    if prompt_yes "Add $PROJECT_DIR/bin to PATH for this session so setup can continue?"; then
      export PATH="$PROJECT_DIR/bin:$PATH"
      echo "✅ PATH updated for this session"
      if prompt_yes "Add $PROJECT_DIR/bin to your ~/.bashrc so it's permanent for future shells?"; then
        # Append the export line to ~/.bashrc (use a marker to avoid duplicates)
        if ! grep -q "# ESP-ProMicro-HidKey arduino-cli" "$HOME/.bashrc" 2>/dev/null; then
          echo "\n# ESP-ProMicro-HidKey arduino-cli" >> "$HOME/.bashrc"
          echo "export PATH=\"$PROJECT_DIR/bin:\$PATH\"" >> "$HOME/.bashrc"
          echo "✅ Added PATH update to ~/.bashrc"
        else
          echo "⚠ PATH entry already present in ~/.bashrc"
        fi
      fi
    else
      echo "\n⚠ Skipping PATH modification. You will need to add $PROJECT_DIR/bin to PATH to use arduino-cli."
    fi
  else
    if [ "$INSTALL_ARDUINO_CLI" = true ] || prompt_yes "arduino-cli not found. Install automatically?"; then
      echo "\n⬇ Installing arduino-cli..."
      curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

      # After install, try to find where it landed
      if command -v arduino-cli >/dev/null 2>&1; then
        echo "✅ arduino-cli installed: $(command -v arduino-cli)"
      elif [ -x "$PROJECT_DIR/bin/arduino-cli" ]; then
        echo "✅ arduino-cli installed into $PROJECT_DIR/bin"
        export PATH="$PROJECT_DIR/bin:$PATH"
        echo "✅ PATH updated for this session (includes $PROJECT_DIR/bin)"
        if prompt_yes "Add $PROJECT_DIR/bin to your ~/.bashrc so it's permanent for future shells?"; then
          if ! grep -q "# ESP-ProMicro-HidKey arduino-cli" "$HOME/.bashrc" 2>/dev/null; then
            echo "\n# ESP-ProMicro-HidKey arduino-cli" >> "$HOME/.bashrc"
            echo "export PATH=\"$PROJECT_DIR/bin:\$PATH\"" >> "$HOME/.bashrc"
            echo "✅ Added PATH update to ~/.bashrc"
          else
            echo "⚠ PATH entry already present in ~/.bashrc"
          fi
        fi
      else
        echo "\n[ERROR] arduino-cli not found in PATH after installation. You may need to add $HOME/bin to PATH and re-open your shell.";
      fi
    else
      echo "\n⚠ Skipping arduino-cli install. You must install it to build and upload firmware.";
    fi
  fi
fi

# 5) Configure Arduino cores and libraries if arduino-cli is available
if command -v arduino-cli >/dev/null 2>&1; then
  echo "\n🔧 Configuring arduino-cli (SparkFun AVR core + AESLib)"
  # Initialize config if missing
  if [ ! -f "$HOME/.arduino15/arduino-cli.yaml" ]; then
    arduino-cli config init || true
  fi

  # Add SparkFun board manager URL
  arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/sparkfun/Arduino_Boards/master/IDE_Board_Manager/package_sparkfun_index.json || true

  echo "Updating core index..."
  arduino-cli core update-index

  echo "Installing SparkFun AVR core: sparkfun:avr"
  arduino-cli core install sparkfun:avr || true

  echo "Installing AESLib library"
  arduino-cli lib install AESLib || true
fi

# 6) Optionally copy .env.template -> .env
if [ ! -f "$PROJECT_DIR/.env" ]; then
  if [ "$COPY_ENV" = true ] || prompt_yes "Copy .env.template to .env (you must edit it with real passwords)?"; then
    if [ -f "$PROJECT_DIR/.env.template" ]; then
      cp "$PROJECT_DIR/.env.template" "$PROJECT_DIR/.env"
      echo "\n✅ .env created from .env.template. Edit .env with your passwords before building."
    else
      echo "[ERROR] .env.template not found in project root."
    fi
  else
    echo "\n⚠ Skipping .env creation. Remember to 'cp .env.template .env' before building."
  fi
else
  echo "\n✅ .env already exists (preserved)."
fi

# 7) Final verification
echo "\n🔎 Running verification script to confirm setup..."
if "$PROJECT_DIR/verify.sh" >/dev/null 2>&1; then
  echo "\n✅ Verification script ran (see output below):"
  "$PROJECT_DIR/verify.sh" || true
else
  echo "\n⚠ Could not run verify.sh automatically; run './verify.sh' to check system status."
fi

echo "\n🎉 Setup complete. Next steps:"
echo "  1) Edit .env with your real passwords (if you created it)."
echo "  2) Run ./build.sh to generate firmware and encrypt secrets."
echo "  3) Run ./upload.sh to flash your device (connect Pro Micro first)."

exit 0
