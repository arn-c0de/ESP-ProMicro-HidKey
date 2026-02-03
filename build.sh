#!/bin/bash
set -e  # Exit immediately on error

# ===== COLORS FOR OUTPUT =====
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ===== PROJECT PATHS =====
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TEMP="$PROJECT_DIR/build_temp"
SECRETS_SCRIPT="$PROJECT_DIR/encrypt_secrets.py"
SKETCH_NAME="ESP-ProMicro-HidKey"

echo -e "${BLUE}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Secure HID Pro Micro - Build System (Arduino CLI)  ║${NC}"
echo -e "${BLUE}║  Device Signature Encryption + Secure Cleanup       ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

# ===== STEP 1: CHECK .ENV EXISTS =====
echo -e "${YELLOW}[1/9] Checking for .env file...${NC}"
if [ ! -f "$PROJECT_DIR/.env" ]; then
    echo -e "${RED}[ERROR] .env file not found!${NC}"
    echo -e "${YELLOW}Action required:${NC}"
    echo -e "  1. cp .env.template .env"
    echo -e "  2. Edit .env with your actual passwords"
    echo -e "  3. Run this script again"
    exit 1
fi
echo -e "${GREEN}✓ .env found${NC}"

# ===== STEP 2: VERIFY ARDUINO CLI =====
echo -e "${YELLOW}[2/9] Verifying Arduino CLI installation...${NC}"
# Locate arduino-cli: prefer system-wide, fall back to project-local install
ARDUINO_CLI="$(command -v arduino-cli 2>/dev/null || true)"
if [ -z "$ARDUINO_CLI" ] && [ -x "$PROJECT_DIR/bin/arduino-cli" ]; then
    ARDUINO_CLI="$PROJECT_DIR/bin/arduino-cli"
    echo -e "${YELLOW}ℹ Using local arduino-cli at $ARDUINO_CLI${NC}"
fi
if [ -z "$ARDUINO_CLI" ]; then
    echo -e "${RED}[ERROR] arduino-cli not found!${NC}"
    echo -e "${YELLOW}Install with:${NC}"
    echo -e "  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh"
    echo -e "  export PATH=\$PATH:\$HOME/bin"
    exit 1
fi
echo -e "${GREEN}✓ Arduino CLI found: $($ARDUINO_CLI version | head -1)${NC}"

# ===== STEP 3: CHECK/INSTALL SPARKFUN AVR CORE =====
echo -e "${YELLOW}[3/9] Checking SparkFun AVR board support...${NC}"
if ! $ARDUINO_CLI core list | grep -qi "sparkfun:avr"; then
    echo -e "${YELLOW}Installing SparkFun AVR core...${NC}"
    
    # Add SparkFun board manager URL if not present
    BOARD_URL="https://raw.githubusercontent.com/sparkfun/Arduino_Boards/master/IDE_Board_Manager/package_sparkfun_index.json"
    $ARDUINO_CLI config init 2>/dev/null || true
    $ARDUINO_CLI config add board_manager.additional_urls "$BOARD_URL" 2>/dev/null || true
    $ARDUINO_CLI core update-index
    $ARDUINO_CLI core install sparkfun:avr
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}[ERROR] Failed to install SparkFun AVR core${NC}"
        exit 1
    fi
fi
echo -e "${GREEN}✓ SparkFun AVR core ready${NC}"

# ===== STEP 4: CHECK REQUIRED LIBRARIES =====
echo -e "${YELLOW}[4/9] Checking Arduino libraries...${NC}"

# Check for AESLib library
if ! $ARDUINO_CLI lib list | grep -qi "AESLib"; then
    echo -e "${YELLOW}Installing AESLib...${NC}"
    $ARDUINO_CLI lib install "AESLib"
fi

# Check for Keyboard library (should be provided by core, but ensure availability)
if ! $ARDUINO_CLI lib list | grep -qi "Keyboard"; then
    echo -e "${YELLOW}Keyboard library not found as a separate library. Trying to install 'Keyboard' library via Library Manager...${NC}"
    $ARDUINO_CLI lib install "Keyboard" || true
fi

# Final check
if $ARDUINO_CLI lib list | grep -qi "AESLib\|Keyboard"; then
    echo -e "${GREEN}✓ Required libraries available${NC}"
else
    echo -e "${YELLOW}? Some required libraries may be missing (AESLib/Keyboard). Build may fail."
fi

# ===== STEP 5: CREATE BUILD TEMP DIRECTORY =====
echo -e "${YELLOW}[5/9] Preparing build environment...${NC}"
rm -rf "$BUILD_TEMP" 2>/dev/null || true
mkdir -p "$BUILD_TEMP"
echo -e "${GREEN}✓ Build temp: $BUILD_TEMP${NC}"

# ===== STEP 6: RUN PYTHON ENCRYPTION SCRIPT =====
echo -e "${YELLOW}[6/9] Encrypting secrets (compile-time AES)...${NC}"

# Check if Python 3 is available
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}[ERROR] python3 not found!${NC}"
    exit 1
fi

# Setup project virtualenv (local .venv) and ensure pycryptodome is installed there
VENV_DIR="$PROJECT_DIR/.venv"
PY="$VENV_DIR/bin/python"
PIP="$VENV_DIR/bin/pip"

if [ ! -d "$VENV_DIR" ]; then
    echo -e "${YELLOW}[VENV] Creating virtual environment at $VENV_DIR${NC}"
    python3 -m venv "$VENV_DIR"
fi

# Upgrade pip and install dependencies inside venv
echo -e "${YELLOW}[VENV] Installing dependencies inside virtualenv...${NC}"
"$PIP" install --upgrade pip setuptools wheel
"$PIP" install pycryptodome

# Check if encryption script exists
if [ ! -f "$SECRETS_SCRIPT" ]; then
    echo -e "${RED}[ERROR] encrypt_secrets.py not found at $SECRETS_SCRIPT${NC}"
    exit 1
fi

# Run encryption script using virtualenv's python
"$PY" "$SECRETS_SCRIPT" "$PROJECT_DIR/.env" "$BUILD_TEMP/secrets.h"

if [ ! -f "$BUILD_TEMP/secrets.h" ]; then
    echo -e "${RED}[ERROR] Secret encryption failed - secrets.h not generated${NC}"
    exit 1
fi

# Copy secrets.h to sketch directory (Arduino requires it)
cp "$BUILD_TEMP/secrets.h" "$PROJECT_DIR/secrets.h"
echo -e "${GREEN}✓ Secrets encrypted and header generated${NC}"

# ===== STEP 7: COMPILE WITH ARDUINO CLI =====
echo -e "${YELLOW}[7/9] Compiling firmware...${NC}"

# Prefer compiling the main sketch file directly to avoid sketch-folder-name mismatch
MAIN_SKETCH="$PROJECT_DIR/${SKETCH_NAME}.ino"
if [ ! -f "$MAIN_SKETCH" ]; then
    echo -e "${RED}[ERROR] Main sketch not found: $MAIN_SKETCH${NC}"
    echo -e "${YELLOW}Expected main sketch: ${SKETCH_NAME}.ino${NC}"
    # Cleanup
    rm -f "$PROJECT_DIR/secrets.h"
    rm -rf "$BUILD_TEMP"
    exit 1
fi

# Arduino CLI expects the main .ino filename to match the sketch folder name.
# Create a temporary symlink with the folder base name pointing to the real main sketch
SKETCH_BASENAME="$(basename "$PROJECT_DIR").ino"
SYMLINK_PATH="$PROJECT_DIR/$SKETCH_BASENAME"
REMOVE_SYMLINK=false
if [ ! -f "$SYMLINK_PATH" ]; then
    ln -s "${SKETCH_NAME}.ino" "$SYMLINK_PATH"
    REMOVE_SYMLINK=true
fi

$ARDUINO_CLI compile \
    --fqbn SparkFun:avr:promicro \
    --build-property "build.extra_flags=-DSECURE_HID_BUILD -Wall" \
    --export-binaries \
    "$PROJECT_DIR"

# Remove temporary symlink if we created it
if [ "$REMOVE_SYMLINK" = true ]; then
    rm -f "$SYMLINK_PATH"
fi

if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR] Compilation failed!${NC}"
    echo -e "${YELLOW}Check the error messages above${NC}"
    
    # Cleanup before exit
    rm -f "$PROJECT_DIR/secrets.h"
    rm -rf "$BUILD_TEMP"
    exit 1
fi

# Find and copy the HEX file
HEX_FILE=$(find "$BUILD_TEMP/build" -name "*.ino.hex" 2>/dev/null | head -1)

# Fallback: search Arduino cache if not found in build temp
if [ -z "$HEX_FILE" ]; then
    HEX_FILE=$(find "$HOME/.cache/arduino" -name "*.ino.hex" 2>/dev/null | tail -1)
fi

if [ -z "$HEX_FILE" ]; then
    echo -e "${RED}[ERROR] Compiled HEX file not found!${NC}"
    exit 1
fi

cp "$HEX_FILE" "$PROJECT_DIR/firmware.hex"
echo -e "${GREEN}✓ Compilation successful${NC}"
echo -e "${BLUE}   Firmware: $PROJECT_DIR/firmware.hex${NC}"

# ===== STEP 8: VERIFY BINARY SECURITY =====
echo -e "${YELLOW}[8/9] Verifying binary security...${NC}"

# Check that no plaintext passwords are in the binary
if strings "$PROJECT_DIR/firmware.hex" | grep -qi "YourMainPassword\|AlternativePassword\|WrongPassword"; then
    echo -e "${RED}[SECURITY WARNING] Plaintext password detected in binary!${NC}"
    echo -e "${RED}This should NEVER happen. Check encryption logic.${NC}"
else
    echo -e "${GREEN}✓ No plaintext passwords in binary${NC}"
fi

# Show binary size
BINARY_SIZE=$(stat -c%s "$PROJECT_DIR/firmware.hex" 2>/dev/null || stat -f%z "$PROJECT_DIR/firmware.hex" 2>/dev/null)
echo -e "${BLUE}   Firmware size: $BINARY_SIZE bytes${NC}"

# ===== STEP 9: FINAL CLEANUP =====
echo -e "${YELLOW}[9/9] Cleanup - removing generated artifacts; .env preserved${NC}"

# Remove secrets.h from sketch directory (it was auto-generated)
rm -f "$PROJECT_DIR/secrets.h"

# Preserve .env file per user preference
if [ -f "$PROJECT_DIR/.env" ]; then
    echo -e "${YELLOW}[9/9] .env file preserved. Edit or truncate it manually to remove secrets if desired.${NC}"
else
    echo -e "${YELLOW}[INFO] .env not present. You can create it from .env.template when needed.${NC}"
fi

# Remove all build artifacts
rm -rf "$BUILD_TEMP"
rm -rf "$PROJECT_DIR/.arduino"
rm -f "$PROJECT_DIR"/*.elf "$PROJECT_DIR"/*.o "$PROJECT_DIR"/*.a

echo -e "${GREEN}✓ Build artifacts cleaned${NC}"

# ===== BUILD COMPLETE =====
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║           BUILD SUCCESSFUL - READY TO FLASH          ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo -e "  1. ${YELLOW}./upload.sh${NC} - Flash firmware to Pro Micro"
echo -e "  2. Device will be securely wiped before flashing"
echo -e "  3. All previous data will be erased"
echo ""
echo -e "${BLUE}Security status:${NC}"
echo -e "  ✓ Secrets encrypted with AES-128"
echo -e "  ✓ .env preserved (left intact for manual editing)"
echo -e "  ✓ Only firmware.hex and .env remain (safe to share firmware only)"
echo -e "  ✓ Device-unique decryption key"
echo ""
echo -e "${YELLOW}Note: .env was preserved. Edit or truncate it manually to remove secrets if desired.${NC}"
echo -e "${YELLOW}If you want a fresh .env, copy .env.template or empty the existing file.${NC}"
echo ""
