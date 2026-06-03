#!/bin/bash
# Flash-Dump via 1200-Baud-Trick + Caterina-Bootloader
set -uo pipefail
OUT="dump/flash.bin"
mkdir -p dump

# First matching /dev/ttyACM* (glob into an array; never parse `ls`).
first_acm_port() {
    local ports=(/dev/ttyACM*)
    [ -e "${ports[0]}" ] && printf '%s\n' "${ports[0]}"
}

# Schritt 1: Firmware-Port finden
FW_PORT=$(first_acm_port)
if [ -z "$FW_PORT" ]; then
    echo "FEHLER: Kein /dev/ttyACM* gefunden. Gerät eingesteckt?"
    exit 1
fi
echo "Firmware-Port: $FW_PORT — sende 1200-Baud-Reset ..."
stty -F "$FW_PORT" 1200 hupcl
sleep 0.5

# Schritt 2: Warten bis alter Port verschwindet
echo "Warte auf Bootloader ..."
for i in $(seq 1 30); do
    [ ! -e "$FW_PORT" ] && break
    sleep 0.1
done

# Schritt 3: Neuen Port abwarten (Bootloader)
BL_PORT=""
for i in $(seq 1 60); do
    BL_PORT=$(first_acm_port)
    [ -n "$BL_PORT" ] && break
    sleep 0.1
done

if [ -z "$BL_PORT" ]; then
    echo "FEHLER: Bootloader-Port nicht erschienen."
    exit 1
fi
echo "Bootloader-Port: $BL_PORT — lese Flash ..."

if avrdude -v -c avr109 -p atmega32u4 -P "$BL_PORT" -b 57600 -U "flash:r:$OUT:r"; then
    echo "Flash gespeichert: $OUT ($(wc -c < "$OUT") Bytes)"
else
    RC=$?
    echo "avrdude Fehlercode: $RC"
    exit $RC
fi
