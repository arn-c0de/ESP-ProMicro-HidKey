#!/bin/bash

# Build und Flash Script für Pro Micro Leonardo
# Board: SparkFun Pro Micro (ATmega32U4, 5V/16MHz)

ARDUINO_CLI="/media/arn/4E786B03786AE8E3/Projects/Arduino-FOLDER/ESP-ProMicro-HidKey/bin/arduino-cli"
SKETCH_DIR="$(dirname "$(realpath "$0")")"
BOARD="arduino:avr:leonardo"
PORT="${1:-/dev/ttyACM0}"

echo "=== Pro Micro Build & Flash ==="
echo "Sketch: $SKETCH_DIR"
echo "Port: $PORT"
echo ""

# Board Cores installieren falls nötig
if ! $ARDUINO_CLI core list | grep -q "arduino:avr"; then
    echo "Installiere Arduino AVR Core..."
    $ARDUINO_CLI core install arduino:avr
fi

if ! $ARDUINO_CLI core list | grep -q "SparkFun:avr"; then
    echo "Installiere SparkFun AVR Core..."
    $ARDUINO_CLI core install SparkFun:avr --additional-urls https://raw.githubusercontent.com/sparkfun/Arduino_Boards/main/IDE_Board_Manager/package_sparkfun_index.json
fi

# Kompilieren
echo "Kompiliere..."
$ARDUINO_CLI compile -v --fqbn "$BOARD" "$SKETCH_DIR"

if [ $? -ne 0 ]; then
    echo "Kompilierung fehlgeschlagen!"
    exit 1
fi

echo ""
echo "Kompilierung erfolgreich!"
echo ""

# Flashen
echo "Flashe auf $PORT..."
echo "(Falls nötig: Reset-Taste am Pro Micro drücken)"
$ARDUINO_CLI upload -v -p "$PORT" --fqbn "$BOARD" "$SKETCH_DIR"

if [ $? -eq 0 ]; then
    echo ""
    echo "Upload erfolgreich! LED sollte jetzt leuchten."
else
    echo ""
    echo "Upload fehlgeschlagen. Versuche:"
    echo "  1. Reset-Taste drücken und sofort Script starten"
    echo "  2. Anderen Port angeben: ./build.sh /dev/ttyACM1"
fi
