#!/bin/bash
# ╔══════════════════════════════════════════════════════╗
# ║   Secure HID Pro Micro - Quick Flash (No Erase)     ║
# ║   Fast upload without chip erase or reset            ║
# ╚══════════════════════════════════════════════════════╝

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

FIRMWARE_HEX="firmware.hex"

# ===== USB HID UNBIND/REBIND FUNCTIONS =====
unbind_hid_driver() {
    local port=$1
    if [ -z "$port" ]; then return; fi
    
    echo -e "${YELLOW}   Deaktiviere HID-Treiber für sauberen Upload...${NC}"
    
    # Find USB device path for this tty port
    local dev_path=$(udevadm info --query=path --name="$port" 2>/dev/null | grep -o '[0-9]-[0-9]' | head -1)
    
    if [ -n "$dev_path" ]; then
        # Unbind cdc_acm driver
        echo -n "$dev_path:1.0" | sudo tee /sys/bus/usb/drivers/cdc_acm/unbind 2>/dev/null || true
        echo -e "${GREEN}   ✓ HID-Treiber getrennt${NC}"
        sleep 1
    fi
}

rebind_hid_driver() {
    echo -e "${YELLOW}   Reaktiviere HID-Treiber...${NC}"
    # System bindet automatisch nach Upload, aber wir können helfen
    sleep 2
    echo -e "${GREEN}   ✓ System reaktiviert Treiber automatisch${NC}"
}

echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Secure HID Pro Micro - Quick Flash (No Erase)     ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

# ===== STEP 1: CHECK FIRMWARE =====
echo -e "${YELLOW}[1/4] Checking for firmware.hex...${NC}"
if [ ! -f "$FIRMWARE_HEX" ]; then
    echo -e "${RED}[ERROR] firmware.hex not found!${NC}"
    echo -e "${YELLOW}Run ./build.sh first to compile firmware${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Firmware found: $FIRMWARE_HEX${NC}"

# ===== STEP 2: CHECK ARDUINO CLI =====
echo -e "${YELLOW}[2/4] Verifying Arduino CLI...${NC}"
if ! command -v arduino-cli &> /dev/null; then
    echo -e "${RED}[ERROR] arduino-cli not found!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Arduino CLI ready${NC}"

# ===== STEP 3: DETECT PRO MICRO =====
echo -e "${YELLOW}[3/4] Detecting Arduino Pro Micro...${NC}"
echo -e "${YELLOW}   Scanning USB ports...${NC}"

# Try JSON format first, fallback to human-readable
DETECTED_PORT=$( ( arduino-cli board list --format json 2>/dev/null | grep -o '"address":"[^\"]*"' | grep -o '/dev/[^\"]*' | head -1 ) || true )
if [ -z "$DETECTED_PORT" ]; then
    DETECTED_PORT=$(arduino-cli board list 2>/dev/null | grep -o '/dev/[^ ]*' | head -1 || true)
fi

if [ -z "$DETECTED_PORT" ]; then
    echo -e "${YELLOW}[WARN] Auto-detection failed. Press RESET button TWICE quickly on Pro Micro${NC}"
    echo ""
    read -p "Press ENTER after resetting to bootloader mode..." dummy
    
    # Try detection again
    DETECTED_PORT=$( ( arduino-cli board list --format json 2>/dev/null | grep -o '"address":"[^\"]*"' | grep -o '/dev/[^\"]*' | head -1 ) || true )
    if [ -z "$DETECTED_PORT" ]; then
        DETECTED_PORT=$(arduino-cli board list 2>/dev/null | grep -o '/dev/[^ ]*' | head -1 || true)
    fi
    
    if [ -z "$DETECTED_PORT" ]; then
        echo -e "${RED}[ERROR] Cannot detect Pro Micro!${NC}"
        read -p "Enter port manually (e.g., /dev/ttyACM0): " MANUAL_PORT
        DETECTED_PORT="$MANUAL_PORT"
    fi
fi

echo -e "${GREEN}✓ Pro Micro detected: $DETECTED_PORT${NC}"

# ===== STEP 4: FLASH FIRMWARE (NO ERASE) =====
echo -e "${YELLOW}[4/4] Flashing firmware (quick mode - no erase)...${NC}"

# Upload via Arduino CLI
arduino-cli upload \
    --fqbn SparkFun:avr:promicro \
    --port "$DETECTED_PORT" \
    --input-file "$FIRMWARE_HEX" \
    --verify

UPLOAD_STATUS=$?

if [ $UPLOAD_STATUS -eq 0 ]; then
    echo -e "${GREEN}✓ Firmware flashed successfully!${NC}"
else
    echo -e "${RED}[ERROR] Flash failed!${NC}"
    echo -e "${YELLOW}Try ./upload.sh for full erase + upload${NC}"
    exit 1
fi

# Brief wait for device restart
sleep 2

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║          QUICK FLASH SUCCESSFUL - READY              ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}Button Sequences:${NC}"
echo -e "  Unlock:      S S L S  (Short Short Long Short)"
echo -e "  Password 1:  S S      (after unlock)"
echo -e "  Password 2:  L S      (after unlock)"
echo -e "  Fake:        L L      (after unlock)"
echo ""
echo -e "${YELLOW}Note: Quick flash does not erase EEPROM or previous data${NC}"
echo -e "${YELLOW}For full secure wipe, use: ./upload.sh${NC}"
echo ""
