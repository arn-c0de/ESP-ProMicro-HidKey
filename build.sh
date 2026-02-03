#!/bin/bash

# Build und Flash Script für Pro Micro Leonardo
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
# Suche nach .ino-Dateien im Sketch-Verzeichnis
FOUND_INO="$(find "$SKETCH_DIR" -maxdepth 1 -name '*.ino' | head -n1)"
SKETCH_BASENAME="$(basename "$SKETCH_DIR")"
EXPECTED_SKETCH="$SKETCH_DIR/$SKETCH_BASENAME.ino"
TEMP_CREATED=0

if [ -f "$EXPECTED_SKETCH" ]; then
    echo "Gefundener Hauptsketch: $EXPECTED_SKETCH"
    BUILD_DIR="$SKETCH_DIR"
elif [ -n "$FOUND_INO" ]; then
    echo "Erstelle temporäres Build-Verzeichnis und kopiere $FOUND_INO als $SKETCH_BASENAME.ino"
    BUILD_DIR="$(mktemp -d)"
    TMP_BASE="$(basename "$BUILD_DIR")"
    cp "$FOUND_INO" "$BUILD_DIR/$TMP_BASE.ino"
    TEMP_CREATED=1
else
    echo "Keine .ino-Datei im Verzeichnis gefunden. Abbruch."
    exit 1
fi

$ARDUINO_CLI compile -v --fqbn "$BOARD" "$BUILD_DIR"

if [ $? -ne 0 ]; then
    echo "Kompilierung fehlgeschlagen!"
    # cleanup temp dir if created
    if [ "$TEMP_CREATED" -eq 1 ]; then
        rm -rf "$BUILD_DIR"
    fi
    exit 1
fi

echo ""
echo "Kompilierung erfolgreich!"
echo ""

# Flashen
echo "Flashe auf $PORT..."
echo "(Falls nötig: Reset-Taste am Pro Micro drücken)"
$ARDUINO_CLI upload -v -p "$PORT" --fqbn "$BOARD" "$BUILD_DIR"
UPLOAD_EXIT=$?

# cleanup temp dir if created
if [ "$TEMP_CREATED" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

if [ $UPLOAD_EXIT -eq 0 ]; then
    echo ""
    echo "Upload erfolgreich! LED sollte jetzt leuchten."
else
    echo ""
    echo "Upload fehlgeschlagen. Versuche:"
    echo "  1. Reset-Taste drücken und sofort Script starten"
    echo "  2. Anderen Port angeben: ./build.sh /dev/ttyACM1"
    echo "  3. Seriellen Port-Zugriff prüfen (z.B. Benutzer zur 'dialout' Gruppe hinzufügen)"
fi
