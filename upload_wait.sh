#!/bin/bash
# Auto-wait for Pro Micro bootloader

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Pro Micro Bootloader Upload - Auto-Wait Mode       ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${YELLOW}ANLEITUNG:${NC}"
echo -e "  1. Pro Micro per USB anschließen"
echo -e "  2. RESET-Button 2x schnell drücken"
echo -e "  3. LED sollte pulsieren (Bootloader aktiv)"
echo -e "  4. Script wartet automatisch auf Bootloader..."
echo ""
echo -e "${YELLOW}Warte auf Bootloader...${NC}"

# Check if firmware exists
if [ ! -f "firmware.hex" ]; then
    echo -e "${RED}[ERROR] firmware.hex nicht gefunden!${NC}"
    echo "Führe zuerst ./build.sh aus"
    exit 1
fi

# Wait for bootloader (max 60 seconds)
TIMEOUT=60
ELAPSED=0

while [ $ELAPSED -lt $TIMEOUT ]; do
    # Check for any /dev/ttyACM* or /dev/ttyUSB* device
    DETECTED_PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)
    
    if [ -n "$DETECTED_PORT" ]; then
        echo -e "${GREEN}✓ Bootloader erkannt: $DETECTED_PORT${NC}"
        sleep 0.5
        
        # Try to upload
        echo -e "${YELLOW}Flashe Firmware...${NC}"
        if arduino-cli upload --fqbn SparkFun:avr:promicro --port "$DETECTED_PORT" --input-file firmware.hex --verify; then
            echo ""
            echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
            echo -e "${GREEN}║            UPLOAD ERFOLGREICH!                       ║${NC}"
            echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
            echo ""
            echo -e "${BLUE}Device sollte jetzt bereit sein!${NC}"
            echo -e "  • LED sollte 2x blinken beim Start"
            echo -e "  • Unlock: S S L S (kurz kurz lang kurz)"
            echo -e "  • Passwort: S S (kurz kurz)"
            exit 0
        else
            echo -e "${RED}Upload fehlgeschlagen!${NC}"
            exit 1
        fi
    fi
    
    # Show progress
    if [ $((ELAPSED % 5)) -eq 0 ]; then
        echo -e "${YELLOW}  [$ELAPSED/${TIMEOUT}s] Drücke RESET-Button 2x schnell...${NC}"
    fi
    
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done

echo ""
echo -e "${RED}[TIMEOUT] Bootloader nicht erkannt nach ${TIMEOUT} Sekunden${NC}"
echo ""
echo -e "${YELLOW}Troubleshooting:${NC}"
echo -e "  1. USB-Kabel prüfen (Daten-fähig, nicht nur Laden)"
echo -e "  2. RESET-Button 2x schnell drücken (< 0,5s Abstand)"
echo -e "  3. LED sollte pulsieren (atmen)"
echo -e "  4. Anderen USB-Port probieren"
echo -e "  5. dmesg | tail -20 ausführen für Details"
exit 1
