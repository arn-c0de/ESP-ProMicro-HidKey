# Secure HID Pro Micro - Quick Setup Guide

## Prerequisites Checklist

- [ ] Arduino CLI installed
- [ ] Python 3 installed
- [ ] pycryptodome installed (`pip3 install pycryptodome`)
- [ ] SparkFun AVR core installed
- [ ] Pro Micro connected via USB

## Step-by-Step Setup

### 1. Install Dependencies (First Time Only)

```bash
# Install Arduino CLI (Linux)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
export PATH=$PATH:$HOME/bin

# Install Python crypto
pip3 install pycryptodome

# Install SparkFun board support
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/sparkfun/Arduino_Boards/master/IDE_Board_Manager/package_sparkfun_index.json
arduino-cli core update-index
arduino-cli core install sparkfun:avr

# Install AES library
arduino-cli lib install AESLib
```

### 2. Configure Your Passwords

```bash
# Copy template
cp .env.template .env

# Edit with your passwords
nano .env
```

**Set strong passwords:**
```bash
DEVICE_SECRET="YourMainPassword123!"
DEVICE_SECRET_2="AlternativePassword456!"
FAKE_PASSWORD="WrongPassword789!"
KDF_SALT="UniqueDeviceSalt2026"
```

### 3. Build Firmware

```bash
./build.sh
```

**What happens:**
1. Scripts **automatically create and use a local Python virtualenv** (`.venv`) if missing
2. Python script encrypts passwords with AES-128 (inside `.venv`)
3. Generates `secrets.h` with ciphertext
4. Compiles firmware via Arduino CLI
5. **Preserves `.env` after build (manual truncation/removal recommended)**
6. Creates `firmware.hex` (safe to distribute)

**Expected output:**
```
[1/9] Checking for .env file... ✓
[2/9] Verifying Arduino CLI... ✓
[3/9] Checking SparkFun AVR board support... ✓
[4/9] Checking Arduino libraries... ✓
[5/9] Preparing build environment... ✓
[6/9] Encrypting secrets (compile-time AES)... ✓
[7/9] Compiling firmware... ✓
[8/9] Verifying binary security... ✓
[9/9] Cleanup - removing generated artifacts; .env preserved... ✓
```

⚠️ **IMPORTANT**: After build, `.env` is preserved by default. If you want it removed, delete or truncate it manually.

### 4. Flash Device

```bash
./upload.sh
```

**If auto-detection fails:**
1. Press RESET button on Pro Micro twice quickly
2. LED will fade in/out (bootloader mode)
3. Run `./upload.sh` again within 8 seconds

### 5. Test Device

**Unlock sequence:** S S L S (within 5 seconds)
- S = Short press (< 500ms)
- L = Long press (≥ 500ms)

**LED should blink:**
- 1x per button press
- 5x when unlocked

**Then select password:** S S (for Password 1)

**LED blinks once, then password types automatically!**

## Hardware Wiring

```
Pro Micro D9  ──┬── [Button]
                └── GND

Pro Micro D10 ──[220Ω]──▶|── LED (then to GND)
```

## Troubleshooting Quick Fixes

**Build fails - "arduino-cli not found":**
```bash
export PATH=$PATH:$HOME/bin
```

**Build fails - "pycryptodome not found":**
```bash
pip3 install --user pycryptodome
```

**Upload fails - "Device not found":**
1. Check USB cable (must support data, not just charging)
2. Try different USB port
3. Press RESET twice quickly on Pro Micro
4. Check: `arduino-cli board list`

**Wrong password output:**
- Rebuild: `cp .env.template .env`, edit, `./build.sh`

## Security Verification

**After build, check:**
```bash
# .env should NOT exist (deleted)
ls -la .env  # Should show "No such file"

# firmware.hex should exist
ls -la firmware.hex

# Verify no plaintext in binary
strings firmware.hex | grep -i "YourMainPassword"  # Should return nothing
```

## Quick Reference

| Command | Purpose |
|---------|---------|
| `./build.sh` | Compile + encrypt (deletes .env!) |
| `./upload.sh` | Flash device (wipes first) |
| `cp .env.template .env` | Restore .env for rebuild |

| Sequence | Result |
|----------|--------|
| S S L S | Unlock device |
| S S | Password 1 (after unlock) |
| L S | Password 2 (after unlock) |
| L L | Fake password (after unlock) |

| LED Pattern | Meaning |
|-------------|---------|
| 2 blinks | Boot ready |
| 1 blink | Button registered |
| 5 blinks | Unlock success |
| 3 blinks | Wrong sequence |
| Solid on | Locked (3 fails) |

## Support

- Full docs: See [README.md](README.md)
- Issues: GitHub Issues
- Security: Report privately

---

**Ready to use? Follow steps 2-5 above!** 🚀
