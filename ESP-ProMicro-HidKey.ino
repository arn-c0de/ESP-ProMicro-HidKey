// HID Keyboard with Multi-Password System
// Different button sequences trigger different passwords
// Pro Micro Leonardo (ATmega32U4)

#include <Keyboard.h>
#include <EEPROM.h>
#include "embedded_passwords.h"
#include "aes.h"
#include "build_config.h"

#define LED_PIN 10
#define BUTTON_PIN 9
#define LONG_PRESS_MS 500

#ifndef SEQUENCE_TIMEOUT_MS
#define SEQUENCE_TIMEOUT_MS 3000
#endif
#define MAX_SEQUENCE_LENGTH 20

// Crypto/layout magic numbers
#define HID_KEYCODE_OFFSET 136  // Keyboard.cpp: codes >= 136 are non-printing keys
#define LATIN1_HIGH        0x80 // bytes >= this need DE-layout translation
#define MIN_ENCRYPTED_LEN  17   // 1 flag byte + AES_BLOCKLEN IV, must be exceeded

// EEPROM addresses
#define EEPROM_FAILED_ATTEMPTS_ADDR 0  // Failed attempts: 1 byte
#define EEPROM_MAGIC_ADDR 1            // Magic byte to check initialization
#define EEPROM_MAGIC_VALUE 0xA5        // Magic value indicating EEPROM is initialized

// Brute-force protection
#define MAX_FAILED_ATTEMPTS 5
#define LOCKOUT_MS 30000  // 30 second lockout

// State
int currentSequenceIndex = 0;
unsigned long pressStart = 0;
unsigned long lastAction = 0;
bool buttonWasPressed = false;
bool sequenceRecognized = false;         // Track if we're already processing a recognized sequence

// Brute-force state
int failedAttempts = 0;
unsigned long lockoutStart = 0;
bool isLockedOut = false;

// Buffer for current input sequence
byte currentInput[MAX_SEQUENCE_LENGTH];

// Whether the current typing session uses the German layout (and therefore
// needs the Latin-1 high-byte translation below). The Arduino Keyboard
// library's layout tables only cover ASCII (0x00-0x7F); bytes >= 0x80 are
// otherwise interpreted as raw HID usage codes and produce garbage.
static bool useDeLayout = false;

// Overwrite a buffer and prevent the compiler from eliding the wipe as a dead
// store. The "memory" clobber is the load-bearing part (CERT C MSC06-C); avr-libc
// has no explicit_bzero/memset_s, so we roll our own. SRAM has no remanence, so a
// single pass is sufficient.
static void secureWipe(void* buf, size_t len) {
  memset(buf, 0, len);
  asm volatile("" : : "r"(buf) : "memory");
}

// Press a raw HID usage code with optional Shift / AltGr modifiers and
// release everything. Bypasses the _asciimap layout lookup by adding
// HID_KEYCODE_OFFSET (see Keyboard.cpp: values >= 136 are non-printing keys).
static void sendRawHidKey(uint8_t hid, bool shift, bool altGr) {
  if (shift)  Keyboard.press(0x81);          // KEY_LEFT_SHIFT
  if (altGr)  Keyboard.press(0x86);          // KEY_RIGHT_ALT (AltGr)
  Keyboard.press((uint8_t)(hid + HID_KEYCODE_OFFSET));
  Keyboard.releaseAll();
  delay(4);
}

// Translate a Latin-1 byte > 0x7F into the corresponding DE keyboard key
// combination. Returns true if handled (typed or intentionally dropped).
static bool writeDeLatin1Extra(byte value) {
  switch (value) {
    case 0xE4: sendRawHidKey(0x34, false, false); return true; // ae
    case 0xC4: sendRawHidKey(0x34, true,  false); return true; // AE
    case 0xF6: sendRawHidKey(0x33, false, false); return true; // oe
    case 0xD6: sendRawHidKey(0x33, true,  false); return true; // OE
    case 0xFC: sendRawHidKey(0x2F, false, false); return true; // ue
    case 0xDC: sendRawHidKey(0x2F, true,  false); return true; // UE
    case 0xDF: sendRawHidKey(0x2D, false, false); return true; // sz
    case 0xA7: sendRawHidKey(0x20, true,  false); return true; // section sign  (Shift+3)
    case 0xB0: sendRawHidKey(0x35, true,  false); return true; // degree sign   (Shift+^)
    case 0xB2: sendRawHidKey(0x1F, false, true ); return true; // superscript 2 (AltGr+2)
    case 0xB3: sendRawHidKey(0x20, false, true ); return true; // superscript 3 (AltGr+3)
    case 0xB5: sendRawHidKey(0x10, false, true ); return true; // micro sign    (AltGr+M)
    default:   return false;
  }
}

void initializeStateStorage() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) {
    EEPROM.write(EEPROM_FAILED_ATTEMPTS_ADDR, 0);
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  }
}

void saveFailedAttempts() {
  EEPROM.write(EEPROM_FAILED_ATTEMPTS_ADDR, failedAttempts);
}

void resetFailedAttempts() {
  failedAttempts = 0;
  saveFailedAttempts();
}

// Discard the in-progress input sequence and clear its buffer.
void resetSequence() {
  currentSequenceIndex = 0;
  memset(currentInput, 0, sizeof(currentInput));
}

bool decryptEntryFromProgmem(int entryIndex, byte* plaintextBuffer, int& plaintextLen, uint8_t& contentType) {
  if (entryIndex < 0 || entryIndex >= PASSWORD_ENTRY_COUNT) {
    return false;
  }

  PasswordEntry entry;
  memcpy_P(&entry, &PASSWORD_ENTRIES[entryIndex], sizeof(PasswordEntry));

  if (entry.password_len <= MIN_ENCRYPTED_LEN || entry.password_len > MAX_ENCRYPTED_PASSWORD_LENGTH) {
    return false;
  }

  // Sensitive buffers declared up front so the single fail: cleanup path can
  // wipe them all (declared before any goto to satisfy C++ scoping rules).
  byte encryptedBuffer[MAX_ENCRYPTED_PASSWORD_LENGTH];
  byte iv[AES_BLOCKLEN];
  byte masterKey[AES_KEYLEN];
  AES_ctx masterCtx;

  for (int i = 0; i < entry.password_len; i++) {
    encryptedBuffer[i] = pgm_read_byte(&entry.password[i]);
  }

  if (encryptedBuffer[0] != ENCRYPTION_STAGE_1) {
    goto fail;
  }

  memcpy(iv, encryptedBuffer + 1, sizeof(iv));

  plaintextLen = entry.password_len - 1 - sizeof(iv);
  // Defensive bounds before touching the plaintext buffer / CBC: a tampered or
  // inconsistent header could otherwise overflow decBuffer (plaintextLen >
  // MAX_PLAINTEXT_LENGTH) or make AES_CBC_decrypt read/write a partial block
  // (plaintextLen not a multiple of AES_BLOCKLEN).
  if (plaintextLen <= 0 || plaintextLen > MAX_PLAINTEXT_LENGTH ||
      (plaintextLen % AES_BLOCKLEN) != 0) {
    goto fail;
  }
  memcpy(plaintextBuffer, encryptedBuffer + 1 + sizeof(iv), plaintextLen);

  for (int i = 0; i < AES_KEYLEN; i++) {
    masterKey[i] = pgm_read_byte(&AES_MASTER_KEY[i]);
  }

  AES_init_ctx(&masterCtx, masterKey);
  AES_CBC_decrypt(&masterCtx, iv, plaintextBuffer, plaintextLen);

  if (plaintextLen <= 0) {
    goto fail;
  }

  {
    byte paddingLen = plaintextBuffer[plaintextLen - 1];
    if (paddingLen == 0 || paddingLen > AES_BLOCKLEN || paddingLen > plaintextLen) {
      goto fail;
    }
    for (int i = plaintextLen - paddingLen; i < plaintextLen; i++) {
      if (plaintextBuffer[i] != paddingLen) {
        goto fail;
      }
    }
    plaintextLen -= paddingLen;
  }
  contentType = entry.content_type;

  secureWipe(encryptedBuffer, sizeof(encryptedBuffer));
  secureWipe(masterKey, sizeof(masterKey));
  secureWipe(&masterCtx, sizeof(masterCtx));
  secureWipe(iv, sizeof(iv));
  return true;

fail:
  secureWipe(encryptedBuffer, sizeof(encryptedBuffer));
  secureWipe(masterKey, sizeof(masterKey));
  secureWipe(&masterCtx, sizeof(masterCtx));
  secureWipe(iv, sizeof(iv));
  secureWipe(plaintextBuffer, MAX_PLAINTEXT_LENGTH);
  plaintextLen = 0;
  return false;
}

void writeSecretByte(byte value) {
  if (value == '\r') {
    return;
  }
  if (value == '\n') {
    Keyboard.write(KEY_RETURN);
    delay(8);
    return;
  }
  if (value >= LATIN1_HIGH) {
    if (useDeLayout) {
      writeDeLatin1Extra(value);
    }
    return;
  }
  Keyboard.write(value);
}

// ==================== LED Feedback ====================
static void blink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(LED_PIN, LOW);
    delay(offMs);
  }
}

void blinkSuccess(int times = 4) { blink(times, 100, 100); }    // short double-blinks
void blinkFail()                 { blink(1, 2000, 0); }         // one long blink
void blinkLockout()              { blink(10, 50, 50); }         // fast: lockout
void blinkReady()                { blink(2, 500, 300); }        // 2x long: ready

// ==================== Sequence Matching ====================
/**
 * Checks if currentInput matches the sequence from PASSWORD_ENTRIES[entryIndex]
 * Uses constant-time comparison to prevent timing attacks
 * @param entryIndex Index in PASSWORD_ENTRIES array
 * @param inputLength Length of current input
 * @return true if fully matched, false otherwise
 */
bool sequenceMatches(int entryIndex, int inputLength) {
  if (entryIndex < 0 || entryIndex >= PASSWORD_ENTRY_COUNT) {
    return false;
  }

  // Load sequence data from PROGMEM to RAM
  PasswordEntry entry;
  memcpy_P(&entry, &PASSWORD_ENTRIES[entryIndex], sizeof(PasswordEntry));
  int seqLen = entry.sequence_len;

  // Length must match
  if (inputLength != seqLen) {
    return false;
  }

  // Accumulate-compare all bytes (constant-time at source level; note the PIN
  // space here is tiny, so this is hardening, not a meaningful timing defense).
  byte mismatch = 0;
  for (int i = 0; i < seqLen; i++) {
    byte expectedByte = pgm_read_byte(&entry.sequence[i]);
    mismatch |= (currentInput[i] ^ expectedByte);
  }

  return (mismatch == 0);
}

/**
 * Check all registered combinations
 * @param inputLength Length of current input
 * @return Index of matching combination, or -1 if none matches
 */
int findMatchingPassword(int inputLength) {
  for (int i = 0; i < PASSWORD_ENTRY_COUNT; i++) {
    if (sequenceMatches(i, inputLength)) {
      return i;
    }
  }
  return -1;
}

/**
 * Sends the password for a combination
 * @param entryIndex Index in PASSWORD_ENTRIES array
 */
void executePassword(int entryIndex) {
  if (entryIndex < 0 || entryIndex >= PASSWORD_ENTRY_COUNT) {
    return;
  }

  byte decBuffer[MAX_PLAINTEXT_LENGTH];
  int plaintextLen = 0;
  uint8_t contentType = CONTENT_TYPE_TEXT;
  // A recognized sequence is a legitimate user, so the brute-force counter is
  // reset whether or not decryption then succeeds (guarded to avoid EEPROM wear).
  if (failedAttempts > 0) {
    resetFailedAttempts();
  }

  if (!decryptEntryFromProgmem(entryIndex, decBuffer, plaintextLen, contentType)) {
    blinkFail();
    secureWipe(decBuffer, sizeof(decBuffer));
    return;
  }

  if (contentType == CONTENT_TYPE_GPG_PRIVATE_KEY) {
    Keyboard.begin(KeyboardLayout_en_US);
    useDeLayout = false;
  } else {
    Keyboard.begin(KeyboardLayout_de_DE);
    useDeLayout = true;
  }
  for (int i = 0; i < plaintextLen; i++) {
    writeSecretByte(decBuffer[i]);
    if (contentType == CONTENT_TYPE_GPG_PRIVATE_KEY && (i % 64) == 63) {
      delay(2);
    }
  }
  Keyboard.end();

  // Wipe the plaintext from the stack (single pass; SRAM has no remanence).
  secureWipe(decBuffer, sizeof(decBuffer));

  blinkSuccess();
}

/**
 * Registers a new button input
 * @param pressType 0 for short (< 500ms), 1 for long (>= 500ms)
 */
void processButtonPress(int pressType) {
  // Check if buffer is full BEFORE writing
  if (currentSequenceIndex >= MAX_SEQUENCE_LENGTH) {
    resetSequence();  // Buffer full, discard and restart
  }

  currentInput[currentSequenceIndex] = pressType;
  currentSequenceIndex++;
  lastAction = millis();
  sequenceRecognized = false;  // Reset recognition flag when new input arrives

  // Just collect the button press - recognition happens after SEQUENCE_TIMEOUT_MS.
}

// ==================== Setup & Main Loop ====================
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  
  Keyboard.begin(KeyboardLayout_de_DE);

  initializeStateStorage();

  // Load failed attempts from EEPROM. An out-of-range value means the counter
  // byte is corrupt/uninitialized; fail CLOSED by treating it as "at the limit"
  // so a corrupted cell enters lockout instead of silently clearing it.
  failedAttempts = EEPROM.read(EEPROM_FAILED_ATTEMPTS_ADDR);
  if (failedAttempts > MAX_FAILED_ATTEMPTS) {
    failedAttempts = MAX_FAILED_ATTEMPTS;
    saveFailedAttempts();
  }

  resetSequence();
  lastAction = millis();
  
  // Check if we're starting in lockout state
  if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
    isLockedOut = true;
    lockoutStart = millis();
  }
  
  // Ready indicator
  blinkSuccess(2);
}

void loop() {
  bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  // Lockout check (using unsigned arithmetic to handle millis() overflow)
  if (isLockedOut) {
    if ((unsigned long)(now - lockoutStart) >= LOCKOUT_MS) {
      // End lockout
      isLockedOut = false;
      resetFailedAttempts();
      blinkSuccess(2);  // Short signal: ready again
    } else {
      // Still locked - ignore inputs
      delay(10);
      return;
    }
  }

  // Try to recognize the sequence after SEQUENCE_TIMEOUT_MS of inactivity.
  if (currentSequenceIndex > 0 && !sequenceRecognized) {
    unsigned long inactiveDuration = (unsigned long)(now - lastAction);
    if (inactiveDuration >= SEQUENCE_TIMEOUT_MS) {
      sequenceRecognized = true;

      int matchIndex = findMatchingPassword(currentSequenceIndex);
      if (matchIndex >= 0) {
        // Match found! executePassword() already resets failedAttempts.
        executePassword(matchIndex);
      } else {
        // No match - wrong sequence
        failedAttempts++;
        saveFailedAttempts();
        if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
          isLockedOut = true;
          lockoutStart = millis();
          blinkLockout();
        } else {
          // Ready for new sequence: 2x long blink
          blinkReady();
        }
      }

      resetSequence();
    }
  }

  // Button just pressed
  if (buttonPressed && !buttonWasPressed) {
    pressStart = now;
    buttonWasPressed = true;
  }

  // Button released
  if (!buttonPressed && buttonWasPressed) {
    unsigned long pressDuration = now - pressStart;
    int pressType = (pressDuration >= LONG_PRESS_MS) ? 1 : 0;

    buttonWasPressed = false;

    // Process the button press
    processButtonPress(pressType);
  }

  delay(10);
}
