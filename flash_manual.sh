#!/bin/bash
# ╔══════════════════════════════════════════════════════╗
# ║   Manual Flash - Enter Port Manually                ║
# ╚══════════════════════════════════════════════════════╝

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

FIRMWARE_HEX="firmware.hex"

echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Manual Flash Mode - Direct Port Entry             ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

# Check firmware
if [ ! -f "$FIRMWARE_HEX" ]; then
    echo -e "${RED}[ERROR] firmware.hex not found! Run ./build.sh first${NC}"
    exit 1
fi

echo -e "${YELLOW}Available USB devices:${NC}"
ls -la /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "  (none found)"
echo ""

echo -e "${YELLOW}Instructions:${NC}"
echo "1. Disconnect Pro Micro USB cable"
echo "2. Wait 5 seconds"
echo "3. Connect to a DIFFERENT USB port (avoid USB hubs)"
echo "4. Press RESET button TWICE quickly (LED will fade)"
echo "5. Check which port appears:"
echo ""
echo -e "${GREEN}   Watch for new device:${NC}"
watch -n 1 'ls /dev/ttyACM* 2>/dev/null || echo "Waiting for device..."' &
WATCH_PID=$!

echo ""
read -p "Press ENTER when you see /dev/ttyACM0 appear (or Ctrl+C to abort)..." dummy
kill $WATCH_PID 2>/dev/null || true
echo ""

# Manual port entry
read -p "Enter port (e.g., /dev/ttyACM0): " PORT

if [ ! -e "$PORT" ]; then
    echo -e "${RED}[ERROR] Port $PORT does not exist!${NC}"
    exit 1
fi

echo -e "${GREEN}Using port: $PORT${NC}"
echo ""

# Try upload with longer timeout
echo -e "${YELLOW}Flashing firmware...${NC}"
timeout 30 arduino-cli upload \
    --fqbn SparkFun:avr:promicro \
    --port "$PORT" \
    --input-file "$FIRMWARE_HEX" \
    --verify || {
    echo -e "${RED}[ERROR] Upload failed!${NC}"
    echo ""
    echo -e "${YELLOW}Troubleshooting:${NC}"
    echo "• Try a different USB cable (some are charging-only)"
    echo "• Use a USB 2.0 port directly on your computer (not USB 3.0 or hub)"
    echo "• Check: dmesg | tail -50"
    echo "• Disconnect ALL other USB devices temporarily"
    exit 1
}

echo ""
echo -e "${GREEN}✓ Flash successful!${NC}"
