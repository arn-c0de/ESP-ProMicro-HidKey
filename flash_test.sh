#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/tests/build_test"
FQBN="SparkFun:avr:promicro"
TIMEOUT=60

# ===== TEST AUSWAHL =====
echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║         Pro Micro Test Flash - Wähle Test           ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${YELLOW}Verfügbare Tests:${NC}"
echo "  1) LED durchgängig an (solid)"
echo "  2) LED blinkt langsam"
echo "  3) LED blinkt + HID Tastatur Test"
echo ""
read -p "Wähle Test (1-3): " choice
echo ""

case $choice in
  1)
    SKETCH_DIR="$PROJECT_DIR/tests/test_led_solid"
    echo -e "${GREEN}→ Test: LED durchgängig an${NC}"
    ;;
  2)
    SKETCH_DIR="$PROJECT_DIR/tests/test_led_blink"
    echo -e "${GREEN}→ Test: LED blinkt${NC}"
    ;;
  3)
    SKETCH_DIR="$PROJECT_DIR/tests/test_blink_keyboard"
    echo -e "${GREEN}→ Test: LED blinkt + HID Tastatur${NC}"
    ;;
  *)
    echo -e "${RED}[ERROR] Ungültige Auswahl!${NC}"
    exit 1
    ;;
esac

echo ""
echo -e "${YELLOW}Compiling test sketch...${NC}"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
arduino-cli compile --fqbn "$FQBN" --output-dir "$BUILD_DIR" "$SKETCH_DIR"

# Find compiled hex
HEX_FILE=$(find "$BUILD_DIR" -type f -name '*.hex' | head -n1)
if [ -z "$HEX_FILE" ]; then
  echo -e "${RED}[ERROR] Kein .hex gefunden. Compile fehlgeschlagen.${NC}"
  exit 1
fi

echo -e "${GREEN}✓ Compile erfolgreich: $(basename "$HEX_FILE")${NC}"

echo -e "${YELLOW}Warte auf Bootloader ($(($TIMEOUT))s). Drücke RESET 2x schnell wenn nötig...${NC}"

ELAPSED=0
while [ $ELAPSED -lt $TIMEOUT ]; do
  PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n1 || true)
  if [ -n "$PORT" ]; then
    echo -e "${GREEN}✓ Device erkannt: $PORT${NC}"
    echo -e "${YELLOW}Versuche Upload...${NC}"
    if arduino-cli upload --fqbn "$FQBN" --port "$PORT" --input-file "$HEX_FILE" --verify; then
      echo -e "${GREEN}✓ Upload erfolgreich! Test-Sketch läuft jetzt.${NC}"
      exit 0
    else
      echo -e "${RED}[ERROR] Upload fehlgeschlagen.${NC}"
      exit 1
    fi
  fi

  sleep 1
  ELAPSED=$((ELAPSED + 1))
  if [ $((ELAPSED % 5)) -eq 0 ]; then
    echo -e "${YELLOW}  [$ELAPSED/${TIMEOUT}s] Noch warten...${NC}"
  fi
done

echo -e "${RED}[TIMEOUT] Kein Gerät erkannt. Versuche: anderes Kabel / USB2-Port / Reset 2x schnell${NC}"
exit 2
