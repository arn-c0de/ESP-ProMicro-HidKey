# 🔐 Secure HID Pro Micro

**Production-grade Arduino Pro Micro USB HID Keyboard with compile-time AES-128 encryption and device signature authentication.**

[![Security](https://img.shields.io/badge/Security-AES--128-green)](https://en.wikipedia.org/wiki/Advanced_Encryption_Standard)
[![Board](https://img.shields.io/badge/Board-Pro%20Micro-blue)](https://www.sparkfun.com/products/12640)
[![Build](https://img.shields.io/badge/Build-Arduino%20CLI-orange)](https://arduino.github.io/arduino-cli/)

---

## 🎯 Features

✅ **Compile-time AES-128 encryption** - Passwords encrypted during build, never stored as plaintext  
✅ **Device signature authentication** - Unique ATmega32u4 hardware ID for key derivation  
✅ **Anti-timing protection** - 5-second input window prevents timing analysis  
✅ **Multi-password support** - Multiple passwords with single device  
✅ **Fake password mode** - Duress protection with believable decoy password  
✅ **Lock-after-fail** - Device locks after 3 failed unlock attempts  
✅ **Secure cleanup** - Removes generated headers and build artifacts; `.env` is preserved (manual truncation/removal recommended)  
✅ **Modular architecture** - Clean separation: crypto, HID, utils, config  

---

## 🛡️ Security Model

### Threat Protection

| Threat | Mitigation |
|--------|-----------|
| **Source code leak** | Secrets in `.env` only (gitignored), never in source |
| **Binary reverse engineering** | Only ciphertext in firmware, device signature required |
| **Build machine compromise** | Cleanup removes generated headers and build artifacts; `.env` is preserved and should be managed manually |
| **Device capture** | Device-unique key, cannot decrypt other devices |
| **Timing attacks** | Fixed 5-second window, no immediate feedback |
| **Duress scenarios** | Fake password looks real, logs show failed auth |

### Encryption Flow

```
.env (plaintext)
    ↓
Build-time: Python AES encryption
    ↓
secrets.h (ciphertext only)
    ↓
Compiled firmware (no plaintext)
    ↓
Device flash memory
    ↓
Runtime: Device signature + salt → AES key
    ↓
Decrypt in RAM (wiped after use)
    ↓
USB HID output
```

---

## 🔧 Hardware Requirements

| Component | Specification |
|-----------|---------------|
| **Microcontroller** | Arduino Pro Micro (ATmega32u4) |
| **Button** | Momentary push button (NO) |
| **LED** | 3mm or 5mm LED + 220-330Ω resistor |
| **USB Cable** | Micro-USB cable |

### Pin Configuration

```
Arduino Pro Micro
┌────────────────────────┐
│       USB Port         │
│                        │
│  D9  ──┬── [BUTTON]    │
│        └── GND         │
│                        │
│  D10 ─[220Ω]─▶|─ LED   │
│               └─── GND │
│                        │
└────────────────────────┘
```

| Pin | Function | Connection |
|-----|----------|------------|
| D9 | Button Input | Button → D9, Button → GND (INPUT_PULLUP) |
| D10 | LED Output | D10 → 220Ω resistor → LED anode → GND |
| GND | Ground | Common ground |

---

## 📦 Software Requirements

### 1. Arduino CLI

```bash
# Install Arduino CLI (Linux/macOS)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
export PATH=$PATH:$HOME/bin

# Verify installation
arduino-cli version
```

### 2. Python 3 + Dependencies

```bash
# Install Python crypto library
pip3 install pycryptodome
```

### 3. SparkFun AVR Board Support

```bash
# Add SparkFun board manager URL
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/sparkfun/Arduino_Boards/master/IDE_Board_Manager/package_sparkfun_index.json

# Install SparkFun AVR core
arduino-cli core update-index
arduino-cli core install sparkfun:avr
```

### 4. Required Libraries

```bash
# Install AESLib for encryption
arduino-cli lib install AESLib
```

---

## 🚀 Quick Start

### 1. Clone/Download Project

```bash
cd /path/to/your/projects
git clone <your-repo-url> ESP-promicro-hidkey
cd ESP-promicro-hidkey
```

### 2. Configure Secrets

```bash
# Copy template to .env
cp .env.template .env

# Edit .env with your actual passwords
nano .env
```

**Example `.env`:**
```bash
DEVICE_SECRET="MyMainPassword123!"
DEVICE_SECRET_2="AlternativePassword456!"
FAKE_PASSWORD="WrongPassword789!"
KDF_SALT="YourDeviceSalt2026"
BUILD_ENCRYPTION_ENABLED=true
```

⚠️ **CRITICAL**: Never commit `.env` to git! It's already in `.gitignore`.

### 3. Build Firmware

```bash
# Make scripts executable
chmod +x build.sh upload.sh

# Build (encrypts secrets, compiles, securely deletes .env)
./build.sh
```

**What happens:**
1. Scripts **automatically create and use a local Python virtualenv** (`.venv`) if missing
2. Python script encrypts passwords with AES-128 (inside `.venv`)
3. Generates `secrets.h` with ciphertext
4. Compiles firmware via Arduino CLI
5. **Securely wipes `.env` with 3-pass DoD overwrite**
6. Creates `firmware.hex` (safe to distribute)

### 4. Flash Device

```bash
# Connect Pro Micro via USB
# Run upload script
./upload.sh
```

**What happens:**
1. Detects connected Pro Micro
2. **Performs chip erase** (wipes previous firmware)
3. Flashes new firmware
4. Verifies upload

**Troubleshooting**: If detection fails, press RESET button twice quickly (bootloader mode).

---

## 🎮 Usage

### Button Sequences

All sequences use a **5-second input window** for anti-timing protection.

| Sequence | Action | Description |
|----------|--------|-------------|
| **S S L S** | Unlock | Short, Short, Long, Short |
| **S S** | Password 1 | (after unlock) Short, Short |
| **L S** | Password 2 | (after unlock) Long, Short |
| **L L** | Fake Password | (after unlock) Long, Long |

**Press Types:**
- **Short (S)**: Press < 500ms
- **Long (L)**: Press ≥ 500ms

### LED Feedback

| Pattern | Meaning |
|---------|---------|
| 2 quick blinks | Device boot/ready |
| 1 blink | Button press registered |
| 5 blinks | Unlock successful |
| 3 blinks | Wrong sequence |
| Solid on | Device locked (3 failed attempts) |

### Example Workflow

1. **Connect device** → 2 quick blinks (ready)
2. **Press unlock sequence** (S S L S within 5 seconds)
   - Each press → 1 blink
   - After 5s → 5 blinks (success)
3. **Press password selection** (e.g., S S for Password 1)
   - Each press → 1 blink
   - After 5s → Password typed via USB + Enter
4. Device returns to idle, ready for next unlock

---

## 🔒 Security Best Practices

### During Development

1. ✅ **Never commit `.env`** - Already in `.gitignore`, but verify
2. ✅ **Use strong passwords** - Minimum 16 characters, mixed case + numbers + symbols
3. ✅ **Unique salt per device** - Change `KDF_SALT` for each Pro Micro
4. ✅ **Rebuild after changes** - Always run `./build.sh` after editing `.env`

### Production Deployment

1. ✅ **Verify `.env` deleted** - Check after `./build.sh` completes
2. ✅ **Secure firmware.hex** - Store in encrypted volume or delete after flash
3. ✅ **Physical security** - Pro Micro should be in tamper-evident enclosure
4. ✅ **USB port control** - Use only on trusted computers

### Incident Response

| Scenario | Action |
|----------|--------|
| **Device stolen** | Attacker cannot decrypt without device signature |
| **Firmware extracted** | Only ciphertext visible, unique per device |
| **Duress situation** | Use L L sequence for fake password |
| **3 failed unlocks** | Device locks, requires power cycle |

---

## 📁 Project Structure

```
ESP-promicro-hidkey/
├── ESP-promicro-hidkey.ino    # Main sketch (state machine)
├── crypto_core.ino             # AES decryption + key derivation
├── hid_controller.ino          # USB keyboard output
├── utils.ino                   # LED control + helpers
├── config.ino                  # Device configuration
├── build.sh                    # Build script (Arduino CLI)
├── upload.sh                   # Upload script (secure flash)
├── encrypt_secrets.py          # Python encryption helper
├── .env.template               # Template for secrets
├── .gitignore                  # Protects sensitive files
└── README.md                   # This file
```

**Auto-generated (not in git):**
- `secrets.h` - Generated from `.env`, contains ciphertext
- `firmware.hex` - Compiled binary (safe to share)
- `.env` - preserved after build (truncate or remove manually if desired)

---

## 🐛 Troubleshooting

### Build Issues

**Problem**: `arduino-cli: command not found`  
**Solution**: Install Arduino CLI and add to PATH:
```bash
export PATH=$PATH:$HOME/bin
```

**Problem**: `ImportError: No module named 'Crypto'`  
**Solution**: Install pycryptodome:
```bash
pip3 install pycryptodome
```

**Problem**: `Board sparkfun:avr:promicro16 not found`  
**Solution**: Install SparkFun core:
```bash
arduino-cli core install sparkfun:avr
```

### Upload Issues

**Problem**: Device not detected  
**Solution**: 
1. Check USB cable (data-capable, not charge-only)
2. Press RESET button twice quickly (bootloader mode)
3. Run `arduino-cli board list` to verify

**Problem**: `avrdude: stk500_recv(): programmer is not responding`  
**Solution**: Timing issue, try:
1. Disconnect Pro Micro
2. Run `./upload.sh`
3. When prompted, connect Pro Micro
4. Press RESET twice quickly

### Runtime Issues

**Problem**: LED doesn't blink on boot  
**Solution**: Check LED polarity and resistor value (220-330Ω)

**Problem**: Button not responding  
**Solution**: Verify pin D2 connection and INPUT_PULLUP wiring

**Problem**: Wrong password output  
**Solution**: 
1. Verify `.env` had correct passwords before build
2. Rebuild firmware: `cp .env.template .env`, edit, `./build.sh`

---

## 🔬 Advanced Configuration

### Custom Pin Assignments

Edit [config.ino](config.ino):

```cpp
const int HW_BUTTON_PIN = 2;   // Change to your button pin
const int HW_LED_PIN = 10;     // Change to your LED pin
```

### Custom Sequences

Edit [ESP-promicro-hidkey.ino](ESP-promicro-hidkey.ino):

```cpp
const char SEQ_UNLOCK[] = {'S', 'S', 'L', 'S'};  // Your unlock sequence
const char SEQ_PASSWORD_1[] = {'S', 'S'};        // Password 1 selector
```

### Timing Adjustments

Edit [config.ino](config.ino):

```cpp
const unsigned long TIMING_LONG_PRESS_MS = 500;     // Long press threshold
const unsigned long TIMING_INPUT_WINDOW_MS = 5000;  // Input window
```

---

## 🚧 Roadmap / Future Enhancements

- [ ] **EEPROM key split** - Store half of key in EEPROM for extra security
- [ ] **USB-C variant** - Support Pro Micro USB-C version
- [ ] **Multi-language keyboard** - International keyboard layouts
- [ ] **Challenge-response** - OTP-style authentication
- [ ] **Tamper detection** - GPIO-based case intrusion alert
- [ ] **Firmware self-destruct** - Wipe on unauthorized access attempts

---

## 📜 License

**MIT License** - See LICENSE file for details.

**Security Disclaimer**: This project is provided "as-is" for educational and personal use. No warranty for production security applications. Always conduct proper security audits before deploying in critical environments.

---

## 🤝 Contributing

Contributions welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Test thoroughly (security-critical code!)
4. Submit pull request with detailed description

**Security vulnerabilities**: Report privately to maintainer, do not open public issues.

---

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/yourusername/ESP-promicro-hidkey/issues)
- **Discussions**: [GitHub Discussions](https://github.com/yourusername/ESP-promicro-hidkey/discussions)
- **Security**: security@yourdomain.com (for vulnerabilities)

---

## 🙏 Acknowledgments

- **Arduino Project** - Arduino core and libraries
- **SparkFun** - Pro Micro board design and support
- **AESLib** - Efficient AES implementation for AVR
- **pycryptodome** - Python cryptography library

---

**Built with ❤️ and 🔐 by the community**

*Last updated: 2026-02-03*
