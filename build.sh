#!/bin/bash

# Build and Flash Script for Pro Micro Leonardo
# Board: SparkFun Pro Micro (ATmega32U4, 5V/16MHz)

# Use system arduino-cli if available, otherwise fall back to a local bin/arduino-cli
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
SKETCH_DIR="$(dirname "$(realpath "$0")")"
BOARD="arduino:avr:leonardo"
PORT="${1:-/dev/ttyACM0}"

echo "=== Pro Micro Build & Flash ==="
echo "Sketch: $SKETCH_DIR"
echo "Port: $PORT"
echo ""

# Generate embedded_password.h from .env
echo "Generating password header from .env..."
python3 "$SKETCH_DIR/generate_password_header.py"
if [ $? -ne 0 ]; then
    echo "Error generating password header file!"
    exit 1
fi
echo ""

# Install board cores if needed
if ! $ARDUINO_CLI core list | grep -q "arduino:avr"; then
    echo "Installing Arduino AVR Core..."
    $ARDUINO_CLI core install arduino:avr
fi

if ! $ARDUINO_CLI core list | grep -q "SparkFun:avr"; then
    echo "Installing SparkFun AVR Core..."
    $ARDUINO_CLI core install SparkFun:avr --additional-urls https://raw.githubusercontent.com/sparkfun/Arduino_Boards/main/IDE_Board_Manager/package_sparkfun_index.json
fi

# Compile
# Search for .ino files in sketch directory
FOUND_INO="$(find "$SKETCH_DIR" -maxdepth 1 -name '*.ino' | head -n1)"
SKETCH_BASENAME="$(basename "$SKETCH_DIR")"
EXPECTED_SKETCH="$SKETCH_DIR/$SKETCH_BASENAME.ino"
TEMP_CREATED=0

if [ -f "$EXPECTED_SKETCH" ]; then
    echo "Found main sketch: $EXPECTED_SKETCH"
    BUILD_DIR="$SKETCH_DIR"
elif [ -n "$FOUND_INO" ]; then
    echo "Creating temporary build directory and copying $FOUND_INO as $SKETCH_BASENAME.ino"
    BUILD_DIR="$(mktemp -d)"
    TMP_BASE="$(basename "$BUILD_DIR")"
    cp "$FOUND_INO" "$BUILD_DIR/$TMP_BASE.ino"
    TEMP_CREATED=1
else
    echo "No .ino file found in directory. Aborting."
    exit 1
fi

# Ensure generated headers are in build directory
if [ -f "$SKETCH_DIR/embedded_passwords.h" ]; then
    echo "Copying embedded_passwords.h to $BUILD_DIR"
    cp "$SKETCH_DIR/embedded_passwords.h" "$BUILD_DIR/"
fi

if [ -f "$SKETCH_DIR/embedded_password.h" ]; then
    echo "Copying embedded_password.h to $BUILD_DIR (Compat)"
    cp "$SKETCH_DIR/embedded_password.h" "$BUILD_DIR/"
fi

if [ -f "$SKETCH_DIR/aes.h" ]; then
    echo "Copying aes.h to $BUILD_DIR"
    cp "$SKETCH_DIR/aes.h" "$BUILD_DIR/"
fi

if [ -f "$SKETCH_DIR/button.h" ]; then
    echo "Copying button.h to $BUILD_DIR"
    cp "$SKETCH_DIR/button.h" "$BUILD_DIR/"
fi

if [ -f "$SKETCH_DIR/led.h" ]; then
    echo "Copying led.h to $BUILD_DIR"
    cp "$SKETCH_DIR/led.h" "$BUILD_DIR/"
fi

$ARDUINO_CLI compile -v --fqbn "$BOARD" "$BUILD_DIR"

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    # cleanup temp dir if created
    if [ "$TEMP_CREATED" -eq 1 ]; then
        rm -rf "$BUILD_DIR"
    fi
    exit 1
fi

echo ""
echo "Compilation successful!"
echo ""

# Flash
echo "Flashing to $PORT..."
echo "(If needed: Press reset button on Pro Micro)"
$ARDUINO_CLI upload -v -p "$PORT" --fqbn "$BOARD" "$BUILD_DIR"
UPLOAD_EXIT=$?

# cleanup temp dir if created
if [ "$TEMP_CREATED" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

if [ $UPLOAD_EXIT -eq 0 ]; then
    echo ""
    echo "Upload successful! LED should now be lit."
else
    echo ""
    echo "Upload failed. Try:"
    echo "  1. Press reset button and immediately start script"
    echo "  2. Specify different port: ./build.sh /dev/ttyACM1"
    echo "  3. Check serial port access (e.g., add user to 'dialout' group)"
fi
