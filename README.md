# ESP-ProMicro-HidKey

A multi-password HID keyboard emulator for Arduino Pro Micro Leonardo (ATmega32U4) that triggers different passwords via USB keyboard emulation using button press sequences.

## Overview

This project implements a secure, sequence-based password manager on microcontroller hardware. Different combinations of short and long button presses unlock and transmit distinct passwords through USB keyboard emulation, protected by a two-stage encryption system.

## Features

- **Sequence-Based Activation**: Multiple password combinations triggered by button press patterns
  - Short press (< 500ms) = `0` | Long press (≥ 500ms) = `1`
- **Two-Stage Encryption Architecture**:
  - **Stage 1** (Build-Time): AES-128-CBC with per-password random IV (firmware storage)
  - **Stage 2** (First-Boot): ChaCha20 per-device re-encryption using HKDF-SHA256 derived keys (EEPROM storage)
- **Persistent Brute-Force Protection**: Survives power cycles and device resets
- **Memory Security**: Multi-pass buffer clearing and automatic crypto context zeroing
- **LED Feedback**: Real-time status indicators for user actions
- **Configuration Management**: Simple `.env` file configuration (auto-generated if missing)
- **No Plaintext Storage**: Passwords never appear in source code or version control

## Getting Started

### Requirements

#### Hardware

- Arduino Pro Micro Leonardo (ATmega32U4, 5V/16MHz)
- 1× Pushbutton switch
- 1× LED (any color)
- 1× 220Ω resistor (LED current limiting)
- Breadboard and jumper wires

#### Wiring

```
Pro Micro Pin    Component
─────────────    ─────────────────────────────────
GND              Button (first terminal)
Pin 9            Button (second terminal)
Pin 10           LED anode (+) via 220Ω resistor
GND              LED cathode (-)
```

**Pin Configuration:**
- Button input: Pin 9 (with internal pullup)
- LED output: Pin 10

#### Software

- **Python** 3.6+ with `pycryptodomex` library
- **arduino-cli** (for building and flashing)
- **SparkFun AVR board support** (installed via arduino-cli)

### Installation

**1. Install Python Dependencies (Debian/Ubuntu/Kali):**
```bash
sudo apt install python3-pycryptodomex
```

Or via pip:
```bash
pip3 install pycryptodomex --user
```

**2. Install arduino-cli:**

Linux/macOS:
```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Or via package manager:
```bash
# Debian/Ubuntu
sudo apt install arduino-cli

# macOS
brew install arduino-cli
```

**3. Clone Repository:**
```bash
git clone <your-repo-url>
cd ESP-ProMicro-HidKey
```

## Configuration

### Setting Up Your Environment

**1. Create Configuration File:**
```bash
cp .env.example .env
nano .env  # or your preferred editor
```

**2. Generate AES Master Key:**
```bash
python3 -c "import secrets; print(secrets.token_hex(16))"
# Output example: a7b3c9d2e8f41a6b5c7e9f2d4a8c1b3e
```

**3. Configure `.env` File:**
```ini
# AES-128 Master Key (32 hex characters = 16 bytes)
AES_MASTER_KEY=a7b3c9d2e8f41a6b5c7e9f2d4a8c1b3e

# Password combinations
COMBINATION_COUNT=3

COMBINATION_0_SEQUENCE="0,0,1,0"
COMBINATION_0_PASSWORD="admin123"

COMBINATION_1_SEQUENCE="1,0,0"
COMBINATION_1_PASSWORD="user456"

COMBINATION_2_SEQUENCE="0,1,1,0,1"
COMBINATION_2_PASSWORD="password789"
```

**Sequence Format:**
- `0` = Short button press (< 500ms)
- `1` = Long button press (≥ 500ms)
- Maximum sequence length: 20 presses
- Separate presses with commas

**Example:** Sequence `"0,0,1,0"` represents:
1. Short press
2. Short press
3. Long press
4. Short press

### Building and Flashing

**1. Connect Pro Micro via USB**

**2. Build and Upload:**
```bash
./build.sh [port]
```

The build script automatically:
- Generates `embedded_passwords.h` from `.env`
- Compiles the sketch
- Uploads to the device

**Default Port:** `/dev/ttyACM0`

**Custom Port Example:**
```bash
./build.sh /dev/ttyACM1
```

**Upload Troubleshooting:**
If upload fails, enter bootloader mode:
1. Press the reset button on Pro Micro twice quickly
2. Run `./build.sh` immediately (within 8 seconds)

### Basic Operation

After flashing:
1. Connect the Pro Micro to your target computer
2. Enter the configured button sequence
3. The password is automatically typed via USB keyboard emulation
4. 3-second timeout resets the sequence if no input is received

## LED Status Indicators

| LED Pattern | Meaning |
|---|---|
| 4 quick blinks | Password matched and transmitted successfully |
| Solid 2-second glow | Invalid sequence or no match found |
| 10 rapid blinks | Brute-force lockout activated |
| 2 quick blinks (startup) | Device ready (stage-2 encryption initialized) |

## Security Architecture

This project implements a **two-stage encryption system** to minimize attack surface while providing device-unique protection for each deployed instance.

### Two-Stage Encryption

#### Stage 1: Build-Time Encryption (Python)

Executed during the build process to protect passwords before firmware deployment:

- Passwords encrypted with **AES-128-CBC** (PKCS#7 padding) using the master key from `.env`
- A cryptographically random 16-byte IV generated per password and prepended to ciphertext
- Encrypted data marked with flag `0x01` and embedded in firmware (`embedded_passwords.h`)
- Master key stored in PROGMEM (flash memory)

#### Stage 2: First-Boot Device Re-Encryption

Executed on device first power-on to create device-specific encryption:

- Device generates unique 16-byte Device ID (stored in EEPROM) using improved entropy sampling
- Device-specific 32-byte key derived via **HKDF-SHA256** from `(Master Key || Device ID)`
- Stage-1 entries are:
  1. Decrypted using AES-CBC with stored IV and master key
  2. Re-encrypted using **ChaCha20** with cryptographically random 12-byte nonce
  3. Marked with flag `0x02` and stored in EEPROM
- Derived key kept only in RAM and cleared after re-encryption

### Security Benefits

| Aspect | Benefit |
|---|---|
| **Device-Unique Protection** | Each device holds independent stage-2 ciphertexts even with identical firmware |
| **Cryptographic Strength** | CBC prevents block-pattern leakage; ChaCha20 avoids XOR-based weaknesses |
| **Key Derivation** | HKDF-SHA256 replaces insecure XOR operations with standard KDF |
| **Persistent Storage** | Plaintext only briefly in RAM during processing, immediately cleared |

### Memory Security Practices

- Passwords stored encrypted in flash (stage-1) and EEPROM (stage-2)
- Plaintext only decrypted into RAM during keyboard transmission
- RAM buffers cleared via 3-pass overwrite (0xFF → 0xAA → 0x00)
- AES and ChaCha20 contexts zeroed after use
- Plaintext passwords never in source code or version control

### Persistent Brute-Force Protection

- Failed attempt counter stored in EEPROM
- **Survives power cycles and device resets**
- Threshold: Maximum 5 failed attempts
- Lockout duration: 30 seconds
- Counter only resets on successful password entry
- No bypass via simple reset

### Threat Model and Limitations

#### Protected Against
- ✅ Firmware extraction alone (stage-2 requires EEPROM)
- ✅ USB replay attacks via keyboard sniffing
- ✅ Timing attacks on sequence matching
- ✅ Brute-force bypass via device reset
- ✅ RAM recovery from powered-off device

#### Not Protected Against
- ❌ Combined flash + EEPROM physical extraction
- ❌ Sophisticated hardware attacks (power analysis, fault injection)
- ❌ Physical coercion

#### Design Principles

This device relies on:
1. **Physical Security**: Secure storage/location
2. **Knowledge Factor**: Memorized button sequences
3. **Possession Factor**: Device ownership

**⚠️ Warning:** The ATmega32U4 lacks hardware security features:
- No secure boot mechanism
- No flash/EEPROM read protection
- No trusted execution environment
- Side-channel attacks (power analysis, timing) possible with specialized equipment

**Recommendations:**
- Use only in physically secure environments
- Treat a stolen device as fully compromised
- Do not use for critical high-security applications
- Consider device as two-factor authentication complement (possession + sequence knowledge)
- For critical applications: implement additional tamper detection or destruction mechanisms

## Troubleshooting

### Upload Fails

**Problem:** Sketch does not upload to device

**Solutions:**
1. Manually trigger bootloader: Press reset button twice quickly
2. Immediately run `./build.sh` (within 8 seconds)
3. Verify serial port: `ls /dev/ttyACM*` or `ls /dev/ttyUSB*`
4. Grant user permissions: `sudo usermod -a -G dialout $USER` (then logout/login)

### Compilation Fails

**Problem:** Build script reports compilation errors

**Solutions:**
1. Verify arduino-cli: `arduino-cli version`
2. Check Python version: `python3 --version` (requires 3.6+)
3. Validate `.env` exists: `test -f .env || echo "Missing .env"`
4. Check `.env` syntax (no spaces around `=`, proper quotes)

### Port Not Found

**Solutions by Platform:**
- **Linux**: Usually `/dev/ttyACM0` or `/dev/ttyACM1`
- **macOS**: Use tab completion with `/dev/cu.usbmodem*`
- **Windows**: Check Device Manager; typically `COM3` or `COM4`

List all ports:
```bash
# Linux
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null

# macOS
ls /dev/cu.* 2>/dev/null
```

### Configuration File Missing

**Solution:**
```bash
cp .env.example .env
# Edit .env with your settings
nano .env
```

## Project Structure

```
ESP-ProMicro-HidKey/
├── ESP-ProMicro-HidKey.ino      # Main Arduino sketch
├── aes.h                         # AES-128-CBC implementation
├── chacha20.h                    # ChaCha20 stream cipher
├── sha256.h                      # SHA-256 hash function
├── embedded_passwords.h          # Auto-generated encrypted passwords
├── embedded_passwords_eeprom.h   # Auto-generated stage-2 encryption data
├── generate_password_header.py   # Password encryption generator
├── build.sh                      # Build and flash automation script
├── build_config.h                # Compile-time configuration
├── .env                          # Your password configuration (gitignored)
├── .env.example                  # Example configuration template
├── .gitignore                    # Git ignore rules
├── README.md                     # This file
├── SECURITY_UPGRADE.md           # Security implementation details
└── LICENSE                       # Project license
```

### Key Files

| File | Purpose |
|---|---|
| `ESP-ProMicro-HidKey.ino` | Main application logic, sequence matching, LED control |
| `aes.h` | Stage-1 AES-128-CBC encryption/decryption |
| `chacha20.h` | Stage-2 ChaCha20 stream cipher |
| `sha256.h` | HKDF-SHA256 key derivation |
| `generate_password_header.py` | Reads `.env`, encrypts passwords, generates headers |
| `build.sh` | Orchestrates build process, password generation, upload |

## Customization

### Timing Configuration

Edit timing constants in `ESP-ProMicro-HidKey.ino`:

```cpp
#define LONG_PRESS_MS 500       // Long press threshold (milliseconds)
#define TIMEOUT_MS 3000         // Sequence timeout (milliseconds)
#define MAX_FAILED_ATTEMPTS 5   // Failed attempts before lockout
#define LOCKOUT_MS 30000        // Lockout duration (milliseconds)
#define MAX_SEQUENCE_LENGTH 20  // Maximum button presses per sequence
```

### Pin Configuration

Change hardware pins in `ESP-ProMicro-HidKey.ino`:

```cpp
#define LED_PIN 10       // LED output pin
#define BUTTON_PIN 9     // Button input pin
```

### Adding More Passwords

**Without recompilation:**
1. Update `.env`: increment `COMBINATION_COUNT`
2. Add new entries:
   ```ini
   COMBINATION_3_SEQUENCE="1,1,0"
   COMBINATION_3_PASSWORD="newpassword123"
   ```
3. Run `./build.sh` to regenerate headers and upload

## Technical Details

### Memory Usage

- **Flash**: ~8-10KB (sketch + stage-1 encrypted passwords)
- **SRAM**: ~400 bytes (runtime buffers, depends on password count)
- **EEPROM**: ~500 bytes (device ID + stage-2 encrypted passwords + brute-force counter)
- **ATmega32U4 Specs**: 32KB flash, 2.5KB SRAM, 1KB EEPROM

### Password Storage Architecture

**Stage 1 (Flash, included in firmware):**
```
[Flag: 0x01] [IV: 16 bytes] [AES-CBC ciphertext: variable length]
```

**Stage 2 (EEPROM, device-specific):**
```
[Flag: 0x02] [Nonce: 12 bytes] [ChaCha20 ciphertext: variable length]
```

**Runtime Processing:**
1. Only active password decrypted into SRAM
2. Plaintext transmitted to USB
3. Buffer immediately cleared via 3-pass overwrite

### Sequence Matching

Uses constant-time bitwise XOR accumulation to prevent timing attacks that could leak partial sequence matches through execution time measurements.

### Key Derivation (Stage 2)

HKDF-SHA256 derives device-specific encryption key:
```
PRK = HMAC-SHA256(salt=0x00x00..., IKM = MasterKey || DeviceID)
OKM = HMAC-SHA256(PRK, info="ESP-ProMicro-HidKey" || 0x01, length=32)
```

Result: 32-byte device-unique key (kept in RAM, cleared after use)

## Development Guide

### Modifying Application Logic

1. Edit `ESP-ProMicro-HidKey.ino` for behavior changes
2. Recompile and upload: `./build.sh`
3. No `.env` changes required for logic modifications

### Changing Passwords

No code changes needed:
1. Update `.env` with new credentials
2. Run `./build.sh` to regenerate encryption and upload
3. Device re-encrypts stage-1 passwords on next boot

### Build Process

The `build.sh` script orchestrates:
1. Validates `.env` configuration
2. Runs `generate_password_header.py` to encrypt stage-1 passwords
3. Compiles Arduino sketch with `arduino-cli`
4. Uploads firmware to connected device
5. Device performs stage-2 re-encryption on first boot

### Python Generator Script

The `generate_password_header.py` script:
- Reads and validates `.env` configuration
- Validates sequence format (0/1 only, max 20 presses)
- Generates cryptographically random IVs
- Encrypts each password with **AES-128-CBC**
- Marks entries with stage-1 flag (0x01)
- Outputs C header file with embedded encrypted data
- Runs automatically during build

## Limitations

| Limitation | Details |
|---|---|
| **Sequence Length** | Maximum 20 button presses per combination |
| **Password Length** | Maximum 63 characters (64-byte buffer) |
| **Sequence Prefix Matching** | If one sequence is a prefix of another (e.g., "0,1" and "0,1,0"), the shorter one triggers first |
| **Hardware Security** | ATmega32U4 lacks secure boot, flash protection, or trusted execution environment |
| **Blocking LED** | LED animations block button input during display |
| **Button Debouncing** | Basic software-only debouncing; no hardware filtering |
| **Microcontroller Scope** | Designed for ATmega32U4; modifications needed for other platforms |

## Known Issues and Roadmap

### Potential Improvements

- [ ] Non-blocking LED state machine (allows button input during animations)
- [ ] Hardware button debouncing circuit
- [ ] Multiple button support
- [ ] Exponential backoff for brute-force attempts
- [ ] Display integration for visual feedback
- [ ] EEPROM wear leveling for counter persistence
- [ ] Modular code structure (separate compilation units)
- [ ] Alternative input methods (capacitive touch, etc.)

## Support and Contributions

### Reporting Issues

Please provide:
- Device specifications (Arduino Pro Micro variant, USB interface)
- Exact error messages and output
- `.env` configuration (without sensitive passwords)
- Operating system and relevant version numbers
- Steps to reproduce the issue

### Contributing

Contributions are welcome. Please:
1. Test thoroughly on actual hardware before submitting
2. Follow existing code style and conventions
3. Update documentation and comments
4. Consider security implications of changes
5. Add tests or validation where applicable

## License

[Specify your project license here]

## Disclaimer

This project is provided **as-is** for educational and personal use. The authors and contributors assume no responsibility for:
- Security breaches or data exposure
- Device malfunction or hardware damage
- Loss of access to credentials or systems
- Misuse of the device

**Use at your own risk.** This device is not suitable for protecting critical authentication credentials or high-security applications. Treat a compromised or stolen device as fully compromised.

## Security References

For more information about cryptographic implementations and security practices:
- [NIST SP 800-38A: Block Cipher Modes](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf) (AES-CBC)
- [ChaCha20 and Poly1305](https://tools.ietf.org/html/rfc7539) (IETF RFC 7539)
- [HKDF: A Simpler Approach to Key Derivation](https://eprint.iacr.org/2010/264.pdf) (RFC 5869)

See [SECURITY_UPGRADE.md](SECURITY_UPGRADE.md) for detailed encryption architecture documentation.
