#!/bin/bash
# USB Troubleshooting für Pro Micro

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${YELLOW}║      USB Troubleshooting für Pro Micro              ║${NC}"
echo -e "${YELLOW}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

echo -e "${YELLOW}Ihr Problem: USB error -71 (device descriptor read error)${NC}"
echo ""
echo -e "${RED}Dies ist ein HARDWARE-Problem, kein Software-Problem!${NC}"
echo ""

echo -e "${GREEN}Lösungen (in dieser Reihenfolge versuchen):${NC}"
echo ""

echo -e "${YELLOW}1. USB-KABEL WECHSELN${NC}"
echo "   • Viele Kabel sind NUR zum Laden (ohne Datenleitungen)"
echo "   • Verwenden Sie ein KURZES (<1m) USB-Datenkabel"
echo "   • Testen Sie 2-3 verschiedene Kabel"
echo ""

echo -e "${YELLOW}2. USB 2.0 PORT NUTZEN${NC}"
echo "   • USB 3.0 (blaue Ports) haben oft Kompatibilitätsprobleme"
echo "   • Nutzen Sie einen schwarzen USB 2.0 Port"
echo "   • DIREKT am Computer, KEIN USB-Hub"
echo ""

echo -e "${YELLOW}3. ANDEREN USB-PORT${NC}"
echo "   • Ihr aktueller Port (1-1) scheint defekt"
echo "   • Probieren Sie ALLE verfügbaren Ports durch"
echo ""

echo -e "${YELLOW}4. USB-CONTROLLER RESET${NC}"
echo "   • Laptop: Stecken Sie das Netzteil ab, warten 30 Sek, anstecken"
echo "   • Desktop: Trennen Sie ALLE USB-Geräte, PC neustarten"
echo ""

echo -e "${YELLOW}5. RESET-BUTTON TIMING${NC}"
echo "   • Drücken Sie RESET 2x SCHNELL (< 0.5 Sekunden Abstand)"
echo "   • LED sollte langsam pulsieren (fade in/out)"
echo "   • Sie haben 8 Sekunden Zeit für Upload"
echo ""

echo -e "${YELLOW}6. ANDEREN COMPUTER TESTEN${NC}"
echo "   • Falls verfügbar: Anderen PC/Laptop testen"
echo "   • Wenn es dort funktioniert: Problem mit Ihrem USB-Controller"
echo ""

echo ""
echo -e "${GREEN}Automatische Tests:${NC}"
echo ""

# Check USB devices
echo -e "${YELLOW}Verfügbare USB-Geräte:${NC}"
lsusb | grep -i "arduino\|atmel\|2341" || echo "  Kein Arduino erkannt"
echo ""

# Check USB messages
echo -e "${YELLOW}Letzte USB-Fehler:${NC}"
dmesg | grep -i "usb\|ttyACM" | tail -10
echo ""

# Check USB power
echo -e "${YELLOW}USB Power Management (sollte 'on' sein):${NC}"
for dev in /sys/bus/usb/devices/*/power/control; do
    if [ -f "$dev" ]; then
        echo "$(dirname $dev | xargs basename): $(cat $dev)"
    fi
done | grep -E "1-1|usb1"
echo ""

echo -e "${GREEN}Empfehlung:${NC}"
echo "1. Trennen Sie Pro Micro"
echo "2. Wechseln Sie USB-Kabel UND USB-Port"
echo "3. Nutzen Sie USB 2.0 Port direkt am PC"
echo "4. Versuchen Sie: ./flash_test.sh (Option 1 - LED solid)"
echo ""
