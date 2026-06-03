#!/bin/bash

# Build and Flash script for Pro Micro Leonardo
# Board: SparkFun Pro Micro (ATmega32U4, 5V/16MHz)
# ESP-ProMicro-HidKey v2.0 - Security Upgrade

set -e

# Use system arduino-cli if available, otherwise fall back to a local bin/arduino-cli
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
SKETCH_DIR="$(dirname "$(realpath "$0")")"
BOARD="arduino:avr:leonardo"

# Pinned core versions for reproducible builds (override via env if your setup
# needs different ones). Pinning avoids silently pulling a new/compromised core.
ARDUINO_AVR_VERSION="${ARDUINO_AVR_VERSION:-1.8.6}"
SPARKFUN_AVR_VERSION="${SPARKFUN_AVR_VERSION:-1.1.13}"

# Default values
PORT=""
RESET_EEPROM=0
PORT_EXPLICITLY_SET=0

# Parse arguments: -p|--port PORT and -r|--reset-eeprom
while [ $# -gt 0 ]; do
  case "$1" in
    -p|--port)
      PORT="$2"
      PORT_EXPLICITLY_SET=1
      shift 2
      ;;
    -r|--reset-eeprom|-R)
      RESET_EEPROM=1
      shift
      ;;
    *)
      # If a single positional arg is passed assume it's the port
      PORT="$1"
      PORT_EXPLICITLY_SET=1
      shift
      ;;
  esac
done

detect_serial_ports() {
    local ports=()
    local path
    for path in /dev/ttyACM* /dev/ttyUSB*; do
        if [ -e "$path" ]; then
            ports+=("$path")
        fi
    done
    printf '%s\n' "${ports[@]}"
}

choose_port() {
    local ports=()
    local port_lines
    local selection

    mapfile -t ports < <(detect_serial_ports)

    if [ ${#ports[@]} -eq 0 ]; then
        echo "⚠️  No serial ports detected right now."
        echo "   Connect the board or enter bootloader mode, then rerun with:"
        echo "   ./build.sh --port /dev/ttyACM0"
        exit 1
    fi

    if [ ${#ports[@]} -eq 1 ]; then
        PORT="${ports[0]}"
        echo "🔌 Auto-selected serial port: $PORT"
        return
    fi

    echo "Available serial ports:"
    for i in "${!ports[@]}"; do
        printf "  %d) %s\n" "$((i + 1))" "${ports[$i]}"
    done
    echo ""

    while true; do
        read -r -p "Select upload port [1-${#ports[@]}]: " selection
        if [[ "$selection" =~ ^[0-9]+$ ]] && [ "$selection" -ge 1 ] && [ "$selection" -le "${#ports[@]}" ]; then
            PORT="${ports[$((selection - 1))]}"
            echo "🔌 Selected serial port: $PORT"
            return
        fi
        echo "Invalid selection."
    done
}

if [ "$PORT_EXPLICITLY_SET" -eq 0 ]; then
    choose_port
fi

echo "═══════════════════════════════════════════════════════════"
echo "  ESP-ProMicro-HidKey v2.0 - Build Script"
echo "═══════════════════════════════════════════════════════════"
echo "Sketch: $SKETCH_DIR"
echo "Port: $PORT"
echo ""

# Check if .env exists
if [ ! -f "$SKETCH_DIR/.env" ]; then
    echo "⚠️  Warning: .env file not found!"
    echo "   Creating a template .env with a generated AES_MASTER_KEY and an example combination."

    # Generate a random 16-byte (32 hex chars) AES key using Python's secrets
    AES_KEY=$(python3 - <<'PY'
import secrets
print(secrets.token_hex(16).upper())
PY
)

    # Create the .env (which holds the AES key) with restrictive perms from the start
    umask 077
    cat > "$SKETCH_DIR/.env" <<EOF
# Auto-generated .env for ESP-ProMicro-HidKey
AES_MASTER_KEY=${AES_KEY}
COMBINATION_COUNT=1
COMBINATION_0_SEQUENCE=0,1,0
COMBINATION_0_TYPE=text
COMBINATION_0_PASSWORD=your_password_here
# Alternative for multiline secrets such as ASCII-armored GPG private keys:
# COMBINATION_0_TYPE=gpg-private-key
# COMBINATION_0_GPG_PRIVATE_KEY_FILE=./secrets/private.asc
SEQUENCE_TIMEOUT_MS=3000  # milliseconds of inactivity before sequence recognition
EOF

    chmod 600 "$SKETCH_DIR/.env"
    echo "✅ Created $SKETCH_DIR/.env — edit COMBINATION_* or SEQUENCE_TIMEOUT_MS values, then re-run ./build.sh"
    exit 0
fi

# Step 1: Generate encrypted password header
echo "📝 Generating encrypted secret header..."
cd "$SKETCH_DIR"
if python3 generate_password_header.py; then
    echo "✅ Generated: embedded_passwords.h (AES-CBC)"
else
    echo "❌ Failed to generate password header"
    exit 1
fi
echo ""

# Install board cores if necessary (pinned versions)
if ! $ARDUINO_CLI core list | grep -q "arduino:avr"; then
    echo "Installing Arduino AVR Core ${ARDUINO_AVR_VERSION}..."
    $ARDUINO_CLI core install "arduino:avr@${ARDUINO_AVR_VERSION}"
fi

if ! $ARDUINO_CLI core list | grep -q "SparkFun:avr"; then
    echo "Installing SparkFun AVR Core ${SPARKFUN_AVR_VERSION}..."
    $ARDUINO_CLI core install "SparkFun:avr@${SPARKFUN_AVR_VERSION}" --additional-urls https://raw.githubusercontent.com/sparkfun/Arduino_Boards/main/IDE_Board_Manager/package_sparkfun_index.json
fi

# Generate build_config.h from .env (SEQUENCE_TIMEOUT_MS)
SEQ_TIMEOUT_MS=$(grep -E '^SEQUENCE_TIMEOUT_MS=' "$SKETCH_DIR/.env" | cut -d= -f2 | sed -E 's/[[:space:]]*#.*//' | tr -d '[:space:]' || true)
# Must be a bare integer: it is interpolated directly into a C macro.
if ! [[ "$SEQ_TIMEOUT_MS" =~ ^[0-9]+$ ]]; then
    if [ -n "$SEQ_TIMEOUT_MS" ]; then
        echo "⚠️  SEQUENCE_TIMEOUT_MS='$SEQ_TIMEOUT_MS' is not an integer; falling back to 3000"
    fi
    SEQ_TIMEOUT_MS=3000
fi
# Strip any leading zeros so the C macro is decimal, not an octal literal.
SEQ_TIMEOUT_MS=$((10#$SEQ_TIMEOUT_MS))
cat > "$SKETCH_DIR/build_config.h" <<EOF
// Auto-generated by build.sh
#pragma once
#define SEQUENCE_TIMEOUT_MS ${SEQ_TIMEOUT_MS}
EOF

echo "🛠  Generated build_config.h (SEQUENCE_TIMEOUT_MS=${SEQ_TIMEOUT_MS})"

# Optional: Reset EEPROM (force Stage-2 re-encryption) by uploading reset_eeprom.ino
if [ "$RESET_EEPROM" -eq 1 ]; then
    echo "🔁 Resetting EEPROM state: compiling and uploading reset_eeprom.ino..."
    if ! $ARDUINO_CLI compile -v --fqbn "$BOARD" "$SKETCH_DIR/tools/reset_eeprom/reset_eeprom.ino"; then
        echo "❌ Failed to compile reset_eeprom.ino"
        exit 1
    fi
    if ! $ARDUINO_CLI upload -v -p "$PORT" --fqbn "$BOARD" "$SKETCH_DIR/tools/reset_eeprom/reset_eeprom.ino"; then
        echo "❌ Failed to upload reset_eeprom.ino"
        exit 1
    fi
    echo "✅ EEPROM reset uploaded; waiting 2 seconds for it to run..."
    sleep 2
fi

# Compile
# Always use the main sketch file specifically (avoid compiling EEPROM_Clear.ino)
MAIN_SKETCH="$SKETCH_DIR/ESP-ProMicro-HidKey.ino"

if [ -f "$MAIN_SKETCH" ]; then
    echo "Compiling sketch: $MAIN_SKETCH"
else
    echo "❌ Main sketch not found: $MAIN_SKETCH"
    exit 1
fi

# set -e would abort on a failed compile, so test explicitly to print a message.
if ! $ARDUINO_CLI compile -v --fqbn "$BOARD" "$MAIN_SKETCH"; then
    echo "❌ Kompilierung fehlgeschlagen!"
    exit 1
fi

echo ""
echo "✅ Kompilierung erfolgreich!"
echo ""

# Flash
echo "Flashing to $PORT..."
echo "(If necessary: press the Reset button on the Pro Micro)"
if $ARDUINO_CLI upload -v -p "$PORT" --fqbn "$BOARD" "$MAIN_SKETCH"; then
    echo ""
    echo "═══════════════════════════════════════════════════════════"
    echo "✅ SUCCESS! Device programmed"
    echo "═══════════════════════════════════════════════════════════"
    echo ""
    echo "🔄 First boot behavior:"
    echo "   - Device initializes lockout state in EEPROM"
    echo "   - Encrypted entries remain in flash until requested"
    echo "   - Ready indicator: 2 blinks"
    echo ""
    echo "🎮 Usage:"
    echo "   - Enter button sequence (short/long presses)"
    echo "   - Device will type the configured secret on match"
    echo "   - ${SEQ_TIMEOUT_MS} ms timeout between presses (SEQUENCE_TIMEOUT_MS)"
    echo ""
    echo "🔒 Active protection:"
    echo "   ✅ AES-CBC with IV (instead of ECB)"
    echo "   ✅ No cleartext secrets in source or generated headers"
    echo "   ✅ Multiline secrets supported via file import"
    echo ""
    echo "📖 Review README.md for configuration details"
    echo ""
else
    echo ""
    echo "❌ Upload failed. Try:"
    echo "  1. Press the Reset button and run the script immediately"
    echo "  2. Specify a different port: ./build.sh /dev/ttyACM1"
    echo "  3. Check the USB connection"
    exit 1
fi
