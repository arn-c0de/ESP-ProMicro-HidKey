#!/bin/bash

# ===== COLORS =====
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Secure HID Pro Micro - System Verification         ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASS=0
FAIL=0

# Function to check and report
check() {
    local name="$1"
    local cmd="$2"
    
    if eval "$cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} $name"
        ((PASS++))
        return 0
    else
        echo -e "${RED}✗${NC} $name"
        ((FAIL++))
        return 1
    fi
}

echo -e "${YELLOW}[1] Checking Dependencies${NC}"
check "Arduino CLI installed" "command -v arduino-cli"
check "Python 3 installed" "command -v python3"
check "pip3 installed" "command -v pip3"
# Check for pycryptodome inside project virtualenv if present, otherwise system
if [ -x "$PROJECT_DIR/.venv/bin/python" ]; then
    check "pycryptodome (venv)" "$PROJECT_DIR/.venv/bin/python -c 'import Crypto'"
else
    check "pycryptodome (system)" "python3 -c 'import Crypto'"
fi

echo ""

echo -e "${YELLOW}[2] Checking Arduino Configuration${NC}"
check "SparkFun AVR core" "arduino-cli core list | grep -q 'sparkfun:avr'"
check "AESLib library" "arduino-cli lib list | grep -q 'AESLib'"
echo ""

echo -e "${YELLOW}[3] Checking Project Files${NC}"
check "build.sh exists" "[ -f '$PROJECT_DIR/build.sh' ]"
check "upload.sh exists" "[ -f '$PROJECT_DIR/upload.sh' ]"
check "encrypt_secrets.py exists" "[ -f '$PROJECT_DIR/encrypt_secrets.py' ]"
check "Main sketch exists" "[ -f '$PROJECT_DIR/ESP-promicro-hidkey.ino' ]"
check "crypto_core.ino exists" "[ -f '$PROJECT_DIR/crypto_core.ino' ]"
check "hid_controller.ino exists" "[ -f '$PROJECT_DIR/hid_controller.ino' ]"
check "utils.ino exists" "[ -f '$PROJECT_DIR/utils.ino' ]"
check "config.ino exists" "[ -f '$PROJECT_DIR/config.ino' ]"
check ".env.template exists" "[ -f '$PROJECT_DIR/.env.template' ]"
check ".gitignore exists" "[ -f '$PROJECT_DIR/.gitignore' ]"
echo ""

echo -e "${YELLOW}[4] Checking File Permissions${NC}"
check "build.sh executable" "[ -x '$PROJECT_DIR/build.sh' ]"
check "upload.sh executable" "[ -x '$PROJECT_DIR/upload.sh' ]"
check "encrypt_secrets.py executable" "[ -x '$PROJECT_DIR/encrypt_secrets.py' ]"
echo ""

echo -e "${YELLOW}[5] Checking Security${NC}"
if [ -f "$PROJECT_DIR/.env" ]; then
    echo -e "${GREEN}✓${NC} .env present (preserved by default after build)"
    ((PASS++))
else
    echo -e "${YELLOW}⚠${NC} .env not present (create from .env.template when needed)"
fi

if [ -f "$PROJECT_DIR/secrets.h" ]; then
    echo -e "${RED}✗${NC} secrets.h exists (should be deleted after build)"
    ((FAIL++))
else
    echo -e "${GREEN}✓${NC} secrets.h not present (secure)"
    ((PASS++))
fi

if grep -q ".env" "$PROJECT_DIR/.gitignore" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} .env in .gitignore"
    ((PASS++))
else
    echo -e "${RED}✗${NC} .env not in .gitignore"
    ((FAIL++))
fi
echo ""

echo -e "${YELLOW}[6] Hardware Detection${NC}"
if arduino-cli board list 2>/dev/null | grep -q "tty"; then
    echo -e "${GREEN}✓${NC} Pro Micro detected"
    arduino-cli board list | grep "tty" | head -1
    ((PASS++))
else
    echo -e "${YELLOW}?${NC} No Pro Micro detected (may not be connected)"
fi
echo ""

# Summary
echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}Passed: $PASS${NC}"
echo -e "${RED}Failed: $FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}✓ System ready for build!${NC}"
    echo -e "${YELLOW}Next steps:${NC}"
    echo -e "  1. cp .env.template .env"
    echo -e "  2. Edit .env with your passwords"
    echo -e "  3. ./build.sh"
    echo -e "  4. ./upload.sh"
    exit 0
else
    echo -e "${RED}✗ Please fix the issues above before building${NC}"
    echo -e "${YELLOW}See SETUP.md for installation instructions${NC}"
    exit 1
fi
