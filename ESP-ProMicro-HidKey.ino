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
#define TIMEOUT_MS SEQUENCE_TIMEOUT_MS
#define MAX_SEQUENCE_LENGTH 20

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

// Press a raw HID usage code with optional Shift / AltGr modifiers and
// release everything. Bypasses the _asciimap layout lookup by adding 136
// (see Keyboard.cpp: values >= 136 are treated as non-printing keys).
static void sendRawHidKey(uint8_t hid, bool shift, bool altGr) {
  if (shift)  Keyboard.press(0x81);          // KEY_LEFT_SHIFT
  if (altGr)  Keyboard.press(0x86);          // KEY_RIGHT_ALT (AltGr)
  Keyboard.press((uint8_t)(hid + 136));
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

bool decryptEntryFromProgmem(int entryIndex, byte* plaintextBuffer, int& plaintextLen, uint8_t& contentType) {
  if (entryIndex < 0 || entryIndex >= PASSWORD_ENTRY_COUNT) {
    return false;
  }

  PasswordEntry entry;
  memcpy_P(&entry, &PASSWORD_ENTRIES[entryIndex], sizeof(PasswordEntry));

  if (entry.password_len <= 17 || entry.password_len > MAX_ENCRYPTED_PASSWORD_LENGTH) {
    return false;
  }

  byte encryptedBuffer[MAX_ENCRYPTED_PASSWORD_LENGTH];
  for (int i = 0; i < entry.password_len; i++) {
    encryptedBuffer[i] = pgm_read_byte(&entry.password[i]);
  }

  if (encryptedBuffer[0] != ENCRYPTION_STAGE_1) {
    memset(encryptedBuffer, 0, sizeof(encryptedBuffer));
    return false;
  }

  byte iv[16];
  memcpy(iv, encryptedBuffer + 1, sizeof(iv));

  plaintextLen = entry.password_len - 1 - sizeof(iv);
  memcpy(plaintextBuffer, encryptedBuffer + 1 + sizeof(iv), plaintextLen);

  byte masterKey[16];
  for (int i = 0; i < 16; i++) {
    masterKey[i] = pgm_read_byte(&AES_MASTER_KEY[i]);
  }

  AES_ctx masterCtx;
  AES_init_ctx(&masterCtx, masterKey);
  AES_CBC_decrypt(&masterCtx, iv, plaintextBuffer, plaintextLen);

  if (plaintextLen <= 0) {
    memset(encryptedBuffer, 0, sizeof(encryptedBuffer));
    memset(masterKey, 0, sizeof(masterKey));
    memset(&masterCtx, 0, sizeof(masterCtx));
    memset(iv, 0, sizeof(iv));
    return false;
  }

  byte paddingLen = plaintextBuffer[plaintextLen - 1];
  if (paddingLen == 0 || paddingLen > 16 || paddingLen > plaintextLen) {
    memset(encryptedBuffer, 0, sizeof(encryptedBuffer));
    memset(masterKey, 0, sizeof(masterKey));
    memset(&masterCtx, 0, sizeof(masterCtx));
    memset(iv, 0, sizeof(iv));
    memset(plaintextBuffer, 0, MAX_PLAINTEXT_LENGTH);
    plaintextLen = 0;
    return false;
  }

  for (int i = plaintextLen - paddingLen; i < plaintextLen; i++) {
    if (plaintextBuffer[i] != paddingLen) {
      memset(encryptedBuffer, 0, sizeof(encryptedBuffer));
      memset(masterKey, 0, sizeof(masterKey));
      memset(&masterCtx, 0, sizeof(masterCtx));
      memset(iv, 0, sizeof(iv));
      memset(plaintextBuffer, 0, MAX_PLAINTEXT_LENGTH);
      plaintextLen = 0;
      return false;
    }
  }

  plaintextLen -= paddingLen;
  contentType = entry.content_type;

  memset(encryptedBuffer, 0, sizeof(encryptedBuffer));
  memset(masterKey, 0, sizeof(masterKey));
  memset(&masterCtx, 0, sizeof(masterCtx));
  memset(iv, 0, sizeof(iv));
  return true;
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
  if (value >= 0x80) {
    if (useDeLayout) {
      writeDeLatin1Extra(value);
    }
    return;
  }
  Keyboard.write(value);
}

// ==================== LED Feedback ====================
void blinkSuccess(int times = 4) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void blinkFail() {
  digitalWrite(LED_PIN, HIGH);
  delay(2000);
  digitalWrite(LED_PIN, LOW);
}

void blinkLockout() {
  // Fast blinking indicates lockout
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
}

void blinkReady() {
  // 2x long blink: ready for new sequence
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
    delay(300);
  }
}

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

  // Constant-time comparison: always check all bytes to prevent timing attacks
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
  if (!decryptEntryFromProgmem(entryIndex, decBuffer, plaintextLen, contentType)) {
    blinkFail();
    memset(decBuffer, 0, sizeof(decBuffer));
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

  // Security: Multi-pass clear
  memset(decBuffer, 0xFF, sizeof(decBuffer));
  memset(decBuffer, 0xAA, sizeof(decBuffer));
  memset(decBuffer, 0x00, sizeof(decBuffer));

  blinkSuccess();
  
  // Reset brute-force counter on success
  if (failedAttempts > 0) {
    failedAttempts = 0;
    saveFailedAttempts();
  }
}

/**
 * Registers a new button input
 * @param pressType 0 for short (< 500ms), 1 for long (>= 500ms)
 */
void processButtonPress(int pressType) {
  // Check if buffer is full BEFORE writing
  if (currentSequenceIndex >= MAX_SEQUENCE_LENGTH) {
    // Buffer full, discard and restart
    currentSequenceIndex = 0;
    memset(currentInput, 0, sizeof(currentInput));
  }

  currentInput[currentSequenceIndex] = pressType;
  currentSequenceIndex++;
  lastAction = millis();
  sequenceRecognized = false;  // Reset recognition flag when new input arrives
  
  // Just collect the button press - don't check yet
  // Recognition will happen 3 seconds after last press
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

  // Load failed attempts from EEPROM
  failedAttempts = EEPROM.read(EEPROM_FAILED_ATTEMPTS_ADDR);
  if (failedAttempts > MAX_FAILED_ATTEMPTS) {
    failedAttempts = 0;
    saveFailedAttempts();
  }
  
  currentSequenceIndex = 0;
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
      failedAttempts = 0;
      saveFailedAttempts();
      blinkSuccess(2);  // Short signal: ready again
    } else {
      // Still locked - ignore inputs
      delay(10);
      return;
    }
  }

  // Check for sequence recognition: if 3 seconds pass without button press, try to recognize
  if (currentSequenceIndex > 0 && !sequenceRecognized) {
    unsigned long inactiveDuration = (unsigned long)(now - lastAction);
    if (inactiveDuration >= TIMEOUT_MS) {
      // 3 seconds of inactivity - try to recognize the sequence
      sequenceRecognized = true;
      
      int matchIndex = findMatchingPassword(currentSequenceIndex);
      if (matchIndex >= 0) {
        // Match found!
        executePassword(matchIndex);
        failedAttempts = 0;
        saveFailedAttempts();
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
      
      // Reset for next sequence
      currentSequenceIndex = 0;
      memset(currentInput, 0, sizeof(currentInput));
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
