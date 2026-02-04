# ESP-ProMicro-HidKey

Multi-password HID keyboard emulator for Arduino Pro Micro Leonardo (ATmega32U4). Different button press sequences trigger different passwords via USB keyboard emulation.

## Features

- Multiple password combinations triggered by button sequences
- Short press (< 500ms) = 0, Long press (>= 500ms) = 1
- **Stage-1: AES-128-CBC** encryption with per-password random IV (stored in firmware)
- **Stage-2: ChaCha20** per-device encryption using an HKDF-SHA256 derived key and a per-password nonce (stored in EEPROM)
- **Persistent brute-force protection** with EEPROM storage (survives power cycles)
- LED feedback for status indication
- Configurable via `.env` file (script will auto-create a template if missing)
- No plaintext passwords in source code or VCS
- **RAM hygiene**: multi-pass buffer clearing and AES/ChaCha context zeroing

## Hardware Requirements

### Components

- Arduino Pro Micro Leonardo (ATmega32U4, 5V/16MHz)
- 1x Pushbutton
- 1x LED (any color)
- 1x 220 Ohm resistor (for LED)
- Breadboard and jumper wires

### Wiring Diagram

```
Pro Micro Pin    Component
=============    =========================================
GND         ---- Button (one side)
Pin 9       ---- Button (other side)
Pin 10      ---- LED anode (+) via 220 Ohm resistor
GND         ---- LED cathode (-)
```

### Pin Configuration

- `BUTTON_PIN 9` - Input with internal pullup
- `LED_PIN 10` - Output for status LED

## Software Requirements

- Python 3.6 or higher with **pycryptodomex** library
- arduino-cli
- SparkFun AVR board support

### Installing Python Dependencies

```bash
# Debian/Ubuntu/Kali
sudo apt install python3-pycryptodomex

# Or via pip (if not using system package manager)
pip3 install pycryptodomex --user
```

### Installing arduino-cli

Linux/macOS:
```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Or install via package manager (apt, brew, etc.)

## Setup Instructions

### 1. Clone Repository

```bash
git clone <your-repo-url>
# AES-128 Master Key (32 hex characters = 16 bytes)
# Generate with: python3 -c "import secrets; print(secrets.token_hex(16))"
AES_MASTER_KEY=A7B3C9D2E8F41A6B5C7E9F2D4A8C1B3E

COMBINATION_0_SEQUENCE="0,0,1,0"
COMBINATION_0_PASSWORD="admin123"

COMBINATION_1_SEQUENCE="1,0,0"
COMBINATION_1_PASSWORD="user456"

COMBINATION_2_SEQUENCE="0,1,1,0,1"
COMBINATION_2_PASSWORD="password789"
```

**AES Key Generation:**
```bash
python3 -c "import secrets; print(secrets.token_hex(16))"
```
This generates a cryptographically secure random 128-bit key.BINATION_COUNT=3

COMBINATION_0_SEQUENCE="0,0,1,0"
COMBINATION_0_PASSWORD="admin123"

COMBINATION_1_SEQUENCE="1,0,0"
COMBINATION_1_PASSWORD="user456"

COMBINATION_2_SEQUENCE="0,1,1,0,1"
COMBINATION_2_PASSWORD="password789"
```

**Sequence Format:**
- `0` = Short button press (< 500ms)
- `1` = Long button press (>= 500ms)
- Maximum sequence length: 20 presses
- Separate presses with commas

**Example:** Sequence "0,0,1,0" means:
1. Short press
2. Short press
3. Long press
4. Short press

### 3. Build and Flash

Connect the Pro Micro via USB, then:

```bash
./build.sh [port]
```

The script will:
1. Generate `embedded_passwords.h` from `.env`
2. Compile the sketch
3. Upload to the Pro Micro

Default port is `/dev/ttyACM0`. Specify a different port if needed:
```bash
./build.sh /dev/ttyACM1
```

If upload fails, press the reset button on the Pro Micro twice quickly to enter bootloader mode, then run the script again immediately.

### 4. Usage

After flashing:
1. Connect Pro Micro to target computer
2. Enter the button sequence for desired password
3. Password will be typed automatically when sequence matches
4. 3-second timeout between presses before sequence resets

## LED Status Indicators

| Pattern | Meaning |
|---------|---------|
| 4 quick blinks | Password matched and sent successfully |
| Solid for 2 seconds | Invalid sequence / no match |
| 10 very fast blinks | Brute-force lockout activated |
| 2 quick blinks at startup | Device ready (stage-2 encryption completed) |

## Security Architecture

### Two-Stage Encryption System

This project implements a **two-stage encryption architecture** to reduce attack surface and provide device-unique protection:

#### Stage 1: Build-Time Encryption (Python)
- Passwords are encrypted with **AES-128-CBC** (PKCS#7 padding) using the master key from `.env`
- A random 16-byte IV is generated per password and prepended to the ciphertext
- Encrypted data is marked with flag `0x01` and embedded in firmware (`embedded_passwords.h`)
- Master key remains in PROGMEM (firmware)

#### Stage 2: First-Boot Re-Encryption (ESP Device)
- On first boot, the device generates a 16-byte Device ID (stored in EEPROM) using improved entropy sampling
- A device-specific 32-byte key is derived with **HKDF-SHA256** from (Master Key || Device ID)
- Stage-1 entries are:
  1. Decrypted using AES-CBC with the stored IV and the master key
  2. Re-encrypted using **ChaCha20** with a random 12-byte nonce (nonce stored alongside ciphertext)
  3. Marked with flag `0x02` and stored in EEPROM
- The derived key is kept only in RAM and cleared after use

#### Benefits
- **Device-Unique Encryption**: Each device circulates independent stage-2 ciphertexts even with identical firmware
- **Improved cryptography**: CBC prevents block-pattern leaks; ChaCha20 avoids repeating-key XOR weaknesses and adds performance
- **Better key derivation**: HKDF replaces insecure XOR derivation
- **No plaintext stored persistently**: plaintext exists only briefly in RAM during processing and is zeroed promptly

**Memory Security:**
- Passwords stored encrypted in flash (stage-1) and EEPROM (stage-2)
- Only decrypted into RAM during transmission
- RAM buffer cleared with 3-pass overwrite (0xFF, 0xAA, 0x00)
- AES context cleared from RAM after use
- Plaintext passwords never in source code or version control

### Persistent Brute-Force Protection

- Failed attempt counter stored in EEPROM
- **Survives power cycles and device resets**
- Maximum 5 failed attempts before 30-second lockout
- Counter persists across reboots (no reset bypass)
- Only successful password entry resets counter

### WARNING: Physical Access Limitations

Despite improvements, physical security limitations still exist:

1. **Flash Access**: Stage-1 encrypted data and master key are in flash memory
   - Attackers can read flash via ISP or chip extraction
   - However, they still need the device's EEPROM for stage-2 decryption

2. **EEPROM Access**: Device ID and stage-2 passwords stored in EEPROM
   - Combined flash + EEPROM access allows full compromise
   - Destroying EEPROM makes extracted firmware useless

3. **No Hardware Security Module**: ATmega32U4 has no:
   - Secure boot
   - Flash/EEPROM read protection
   - Trusted execution environment

4. **Side-Channel Attacks**: Power analysis or timing attacks may reveal keys
   - Constant-time comparison mitigates timing attacks on sequence matching
   - Power analysis during decryption still possible

### Threat Model & Recommendations

**Protected Against:**
- ✅ Firmware extraction alone (needs EEPROM too)
- ✅ Replay attacks via USB sniffing
- ✅ Timing attacks on sequence matching
- ✅ Reset-based brute-force bypass
- ✅ RAM dumps after power-off (multi-pass clearing)

**NOT Protected Against:**
- ❌ Physical device compromise with both flash + EEPROM access
- ❌ Sophisticated hardware attacks (power analysis, fault injection)
- ❌ Rubber-hose cryptanalysis (physical coercion)

**Best Practices:**
- Use only in physically secure environments
- Do not use for high-security/critical applications
- Treat stolen device as fully compromised
- Consider device as "two-factor" (possession + sequence knowledge)
- For critical use: add external tamper detection or destruction mechanism


- The primary security model relies on:
  - Physical device security
  - Memorized button sequences (something you know)
  - Device possession (something you have)

## Troubleshooting

### Upload Failed

1. Press the reset button on Pro Micro twice quickly
2. Run `./build.sh` immediately (within 8 seconds)
3. Check serial port: `ls /dev/ttyACM*` or `ls /dev/ttyUSB*`
4. Add user to dialout group: `sudo usermod -a -G dialout $USER` (then logout/login)

### Compilation Failed

1. Verify arduino-cli is installed: `arduino-cli version`
2. Check Python version: `python3 --version` (requires 3.6+)
3. Ensure `.env` file exists and is properly formatted
4. Check for syntax errors in `.env` file

### Wrong Port

- Linux: Usually `/dev/ttyACM0` or `/dev/ttyACM1`
- macOS: `/dev/cu.usbmodem*` (use tab completion)
- Windows: `COM3`, `COM4`, etc. (check Device Manager)

### No .env File

Copy `.env.example` to `.env` and customize:
```bash
cp .env.example .env
nano .env
```

## Project Structure

```
ESP-ProMicro-HidKey/
├── ESP-ProMicro-Test.ino       # Main Arduino sketch with AES decryption
├── aes.h                        # Minimal AES-128-ECB implementation
├── embedded_passwords.h         # Auto-generated (AES-encrypted passwords)
├── generate_password_header.py  # Password encryption generator (AES)
├── button.h                     # Button handler module (optional)
├── led.h                        # LED controller module (optional)
├── build.sh                     # Build and flash script
├── .env                         # Your password config (gitignored)
├── .env.example                 # Example configuration
├── .gitignore                   # Git ignore rules
└── README.md                    # This file
```

## Configuration

### Timing Constants

Edit in `ESP-ProMicro-Test.ino`:

```cpp
#define LONG_PRESS_MS 500       // Long press threshold (ms)
#define TIMEOUT_MS 3000         // Sequence timeout (ms)
#define MAX_FAILED_ATTEMPTS 5   // Attempts before lockout
#define LOCKOUT_MS 30000        // Lockout duration (ms)
#define MAX_SEQUENCE_LENGTH 20  // Maximum button presses per sequence
```

### Pin Assignment

Change pins if needed:

```cpp
#define LED_PIN 10
#define BUTTON_PIN 9
```

## Technical Details

### Memory Usage

- Flash: ~4-6KB (sketch + passwords)
- SRAM: ~400 bytes (depends on password count)
- ATmega32U4 has 32KB flash, 2.5KB SRAM

### Password Storage

Passwords are stored encrypted to minimize persistent exposure:
- Sequences: Byte arrays in flash (PROGMEM)
- Stage-1 (Flash): `flag(0x01) || IV(16B) || AES-CBC ciphertext`
- Stage-2 (EEPROM): `flag(0x02) || Nonce(12B) || ChaCha20 ciphertext`
- Only the active password is decrypted to SRAM briefly during typing
- All sensitive buffers and crypto contexts are cleared immediately after use (multi-pass clearing)

### Constant-Time Comparison

Sequence matching uses bitwise XOR accumulation to prevent timing attacks that could reveal partial matches through execution time differences.

## Development

### Modifying Code

1. Edit `.env` for password changes (no recompilation needed)
2. Edit `ESP-ProMicro-Test.ino` for logic changes
3. Run `./build.sh` to compile and upload

### Adding More Passwords

1. Increment `COMBINATION_COUNT` in `.env`
2. Add new `COMBINATION_N_SEQUENCE` and `COMBINATION_N_PASSWORD` entries
3. Run `./build.sh`

### Python Script

The `generate_password_header.py` script:
- Reads `.env` configuration
- Validates sequences (0/1 only, max length 20)
- Encrypts each password with **AES-128-CBC** using the master key and a random IV
- Prepends a stage-1 flag (0x01) followed by the IV then ciphertext in the generated header
- Runs automatically during build (the build script creates a `.env` template if missing)

## Known Limitations

1. **Buffer Size**: Maximum 20 button presses per sequence
2. **Password Length**: Maximum 63 characters (64 byte buffer)
3. **Sequence Ambiguity**: If one sequence is a prefix of another (e.g., "0,1" and "0,1,0"), the shorter sequence will match first
4. **Hardware Key Protection**: Master key is stored in firmware (PROGMEM) and the ATmega32U4 lacks a hardware secure element or read-protection — physical access can lead to compromise
5. **Blocking LED**: LED animations block button input during display
6. **No Debouncing**: Basic button handling without hardware debouncing

## Future Improvements

Potential enhancements (not implemented):

- EEPROM-based persistent lockout counter
- Non-blocking LED state machine
- Hardware button debouncing
- Support for multiple buttons
- Exponential backoff for failed attempts
- Display integration for visual feedback
- Modular code structure (separate .h/.cpp files)

## License

[Specify your license here - e.g., MIT, GPL, etc.]

## Contributing

Contributions welcome. Please:
1. Test changes thoroughly on hardware
2. Follow existing code style
3. Update documentation
4. Consider security implications

## Disclaimer

This project is provided as-is for educational and personal use. The authors take no responsibility for any security breaches, data loss, or misuse of this device. Use at your own risk. Do not use for applications requiring high security.

## Support

For issues, questions, or contributions:
- Open an issue on GitHub
- Check existing issues for solutions
- Include hardware details and error messages
