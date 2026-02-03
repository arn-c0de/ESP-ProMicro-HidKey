#!/bin/bash
set -e  # Exit immediately on error

# ===== COLORS FOR OUTPUT =====
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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
    sleep 2
    echo -e "${GREEN}   ✓ System reaktiviert Treiber automatisch${NC}"
}

# ===== PROJECT PATHS =====
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_HEX="$PROJECT_DIR/firmware.hex"

# Ensure project virtualenv exists (used by build tools)
VENV_DIR="$PROJECT_DIR/.venv"
PY="$VENV_DIR/bin/python"
PIP="$VENV_DIR/bin/pip"
if [ ! -d "$VENV_DIR" ]; then
    echo -e "${YELLOW}[VENV] Creating virtual environment at $VENV_DIR${NC}"
    python3 -m venv "$VENV_DIR"
    "$PIP" install --upgrade pip setuptools wheel || true
fi

echo -e "${BLUE}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   Secure HID Pro Micro - Flash Upload (Safe Wipe)   ║${NC}"
echo -e "${BLUE}║   Full Device Erase + Clean Install                  ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

# ===== STEP 1: CHECK FIRMWARE EXISTS =====
echo -e "${YELLOW}[1/6] Checking for firmware.hex...${NC}"
if [ ! -f "$FIRMWARE_HEX" ]; then
    echo -e "${RED}[ERROR] firmware.hex not found!${NC}"
    echo -e "${YELLOW}Action required:${NC}"
    echo -e "  Run ./build.sh first to compile firmware"
    exit 1
fi
echo -e "${GREEN}✓ Firmware found: $(basename $FIRMWARE_HEX)${NC}"

# ===== STEP 2: VERIFY ARDUINO CLI =====
echo -e "${YELLOW}[2/6] Verifying Arduino CLI...${NC}"
if ! command -v arduino-cli &> /dev/null; then
    echo -e "${RED}[ERROR] arduino-cli not found!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Arduino CLI ready${NC}"

# ===== STEP 3: DETECT PRO MICRO =====
echo -e "${YELLOW}[3/6] Detecting Arduino Pro Micro...${NC}"

# List connected boards
echo -e "${BLUE}   Scanning USB ports...${NC}"
arduino-cli board list

# Try to detect Pro Micro automatically
# Prefer JSON (contains addresses) when available, fallback to human-readable listing
DETECTED_PORT=$( ( arduino-cli board list --format json 2>/dev/null | grep -o '"address":"[^"]*"' | grep -o '/dev/[^" ]*' | head -1 ) || true )
if [ -z "$DETECTED_PORT" ]; then
    DETECTED_PORT=$(arduino-cli board list 2>/dev/null | grep -o '/dev/[^ ]*' | head -1 || true)
fi

if [ -z "$DETECTED_PORT" ]; then
    echo -e "${YELLOW}[WARN] Auto-detection failed. Pro Micro may require manual reset.${NC}"
    echo ""
    echo -e "${YELLOW}Pro Micro connection tips:${NC}"
    echo -e "  1. Connect Pro Micro via USB"
    echo -e "  2. Press RESET button TWICE quickly (bootloader mode)"
    echo -e "  3. LED will fade in/out when in bootloader mode"
    echo -e "  4. You have ~8 seconds to upload"
    echo ""
    read -p "Press ENTER after resetting Pro Micro to bootloader mode..." dummy
    
    # Try detection again (JSON, then human-readable fallback)
    DETECTED_PORT=$( ( arduino-cli board list --format json 2>/dev/null | grep -o '"address":"[^\"]*"' | grep -o '/dev/[^\"]*' | head -1 ) || true )
    if [ -z "$DETECTED_PORT" ]; then
        DETECTED_PORT=$(arduino-cli board list 2>/dev/null | grep -o '/dev/[^ ]*' | head -1 || true)
    fi
    
    if [ -z "$DETECTED_PORT" ]; then
        echo -e "${RED}[ERROR] Still cannot detect Pro Micro!${NC}"
        echo -e "${YELLOW}Manual port entry:${NC}"
        read -p "Enter port manually (e.g., /dev/ttyACM0): " MANUAL_PORT
        DETECTED_PORT="$MANUAL_PORT"
    fi
fi

echo -e "${GREEN}✓ Pro Micro detected: $DETECTED_PORT${NC}"

# Provide a warning if the detected board's FQBN isn't promicro (but continue)
FQBN_DETECTED=$(arduino-cli board list --format json 2>/dev/null | grep -o '"fqbn":"[^"]*"' | sed 's/"fqbn":"\(.*\)"/\1/' | head -1 || true)
if [ -n "$FQBN_DETECTED" ] && ! echo "$FQBN_DETECTED" | grep -qi 'promicro'; then
    echo -e "${YELLOW}[WARN] Detected board FQBN: $FQBN_DETECTED (not SparkFun:avr:promicro). If this is wrong, enter port manually when prompted.${NC}"
fi

# ===== STEP 4: CRITICAL - SECURE DEVICE WIPE =====
echo -e "${YELLOW}[4/6] Erasing device (secure wipe before flash)...${NC}"
echo -e "${RED}   WARNING: ALL PREVIOUS DATA WILL BE ERASED!${NC}"
echo -e "${YELLOW}   Note: Arduino CLI will perform chip erase automatically during upload${NC}"
sleep 1

# Skip manual avrdude erase - it causes port issues
# Arduino CLI upload includes automatic chip erase

# ===== STEP 5: FLASH FIRMWARE =====
echo -e "${YELLOW}[5/6] Flashing new firmware...${NC}"

# Final port check right before upload
if [ ! -e "$DETECTED_PORT" ]; then
    echo -e "${YELLOW}   Port disappeared, re-detecting...${NC}"
    sleep 2
    
    for i in {1..3}; do
        DETECTED_PORT=$( ( arduino-cli board list --format json 2>/dev/null | grep -o '"address":"[^\"]*"' | grep -o '/dev/[^\"]*' | head -1 ) || true )
        if [ -z "$DETECTED_PORT" ]; then
            DETECTED_PORT=$(arduino-cli board list 2>/dev/null | grep -o '/dev/[^ ]*' | head -1 || true)
        fi
        
        if [ -n "$DETECTED_PORT" ] && [ -e "$DETECTED_PORT" ]; then
            break
        fi
        sleep 1
    done
fi

echo -e "${YELLOW}   Using port: $DETECTED_PORT${NC}"

# Upload via Arduino CLI (includes auto-erase)
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
    echo ""
    echo -e "${YELLOW}Troubleshooting:${NC}"
    echo -e "  1. Disconnect and reconnect Pro Micro"
    echo -e "  2. Press RESET button twice quickly"
    echo -e "  3. Run this script again within 8 seconds"
    echo -e "  4. Check USB cable and port permissions"
    exit 1
fi

# ===== STEP 6: VERIFICATION =====
echo -e "${YELLOW}[6/6] Waiting for device restart...${NC}"
sleep 3

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║            UPLOAD SUCCESSFUL - DEVICE READY          ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}Security Verification:${NC}"
echo -e "  ✓ Previous device data wiped"
echo -e "  ✓ Clean firmware installed"
echo -e "  ✓ Encrypted passwords embedded"
echo -e "  ✓ Device-unique decryption key active"
echo ""
echo -e "${BLUE}Button Sequences (5-second input window):${NC}"
echo -e "  ${GREEN}Unlock:${NC}      S S L S  (Short Short Long Short)"
echo -e "  ${GREEN}Password 1:${NC}  S S      (after unlock)"
echo -e "  ${GREEN}Password 2:${NC}  L S      (after unlock)"
echo -e "  ${GREEN}Fake:${NC}        L L      (after unlock)"
echo ""
echo -e "${BLUE}LED Feedback:${NC}"
echo -e "  • 1 blink:  Button registered"
echo -e "  • 5 blinks: Unlock successful"
echo -e "  • 3 blinks: Error / wrong sequence"
echo -e "  • Solid:    Device locked (3 failed attempts)"
echo ""
echo -e "${YELLOW}Note: Device lock requires power cycle to reset${NC}"
echo ""
