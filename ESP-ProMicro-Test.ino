// HID Keyboard with Multi-Password System
// Different button sequences trigger different passwords
// Pro Micro Leonardo (ATmega32U4)

#include <Keyboard.h>
#include <EEPROM.h>
#include "embedded_passwords.h"
#include "aes.h"

#define LED_PIN 10
#define BUTTON_PIN 9
#define LONG_PRESS_MS 500
#define TIMEOUT_MS 3000
#define MAX_SEQUENCE_LENGTH 20

// EEPROM addresses
#define EEPROM_DEVICE_ID_ADDR 0        // Device ID: 16 bytes (0-15)
#define EEPROM_FAILED_ATTEMPTS_ADDR 16 // Failed attempts: 1 byte (16)
#define EEPROM_MAGIC_ADDR 17           // Magic byte to check initialization (17)
#define EEPROM_MAGIC_VALUE 0xA5        // Magic value indicating EEPROM is initialized

// Brute-force protection
#define MAX_FAILED_ATTEMPTS 5
#define LOCKOUT_MS 30000  // 30 second lockout

// State
int currentSequenceIndex = 0;
unsigned long pressStart = 0;
unsigned long lastAction = 0;
bool buttonWasPressed = false;

// Brute-force state
int failedAttempts = 0;
unsigned long lockoutStart = 0;
bool isLockedOut = false;

// Buffer for current input sequence
byte currentInput[MAX_SEQUENCE_LENGTH];

// Device-specific encryption key (derived from master key + device ID)
byte derivedKey[AES_KEYLEN];

// ==================== Device ID & Key Derivation ====================
void initializeDeviceID() {
  // Check if EEPROM is already initialized
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
    // Already initialized, load failed attempts counter
    failedAttempts = EEPROM.read(EEPROM_FAILED_ATTEMPTS_ADDR);
    if (failedAttempts > MAX_FAILED_ATTEMPTS) {
      failedAttempts = 0; // Corrupted value, reset
    }
    return;
  }
  
  // First boot: Generate random device ID
  randomSeed(analogRead(A0) ^ micros());
  for (int i = 0; i < 16; i++) {
    byte randomByte = random(256);
    EEPROM.write(EEPROM_DEVICE_ID_ADDR + i, randomByte);
  }
  
  // Initialize failed attempts counter
  EEPROM.write(EEPROM_FAILED_ATTEMPTS_ADDR, 0);
  
  // Set magic byte
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  
  failedAttempts = 0;
}

void deriveEncryptionKey() {
  // Read master key directly from PROGMEM (no device ID derivation)
  // This matches the encryption in Python which uses the master key directly
  for (int i = 0; i < 16; i++) {
    derivedKey[i] = pgm_read_byte(&AES_MASTER_KEY[i]);
  }
}

void saveFailedAttempts() {
  EEPROM.write(EEPROM_FAILED_ATTEMPTS_ADDR, failedAttempts);
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

  // Load entry from PROGMEM to RAM
  PasswordEntry entry;
  memcpy_P(&entry, &PASSWORD_ENTRIES[entryIndex], sizeof(PasswordEntry));

  // AES decryption of password
  char buffer[64];
  byte encryptedBlock[AES_BLOCKLEN];
  
  // Initialize AES context with derived key
  struct AES_ctx ctx;
  AES_init_ctx(&ctx, derivedKey);
  
  // Decrypt password (may span multiple AES blocks)
  int numBlocks = (entry.password_len + AES_BLOCKLEN - 1) / AES_BLOCKLEN;
  int decryptedLen = 0;
  
  for (int block = 0; block < numBlocks && decryptedLen < (int)sizeof(buffer) - 1; block++) {
    // Read encrypted block from PROGMEM
    for (int i = 0; i < AES_BLOCKLEN; i++) {
      encryptedBlock[i] = pgm_read_byte(&entry.password[block * AES_BLOCKLEN + i]);
    }
    
    // Decrypt block
    AES_ECB_decrypt(&ctx, encryptedBlock);
    
    // Copy decrypted data to buffer (up to plaintext_len)
    int copyLen = min(AES_BLOCKLEN, entry.plaintext_len - decryptedLen);
    memcpy(buffer + decryptedLen, encryptedBlock, copyLen);
    decryptedLen += copyLen;
    
    // Clear encrypted block from RAM
    volatile byte* vptr_enc = (volatile byte*)encryptedBlock;
    for (int i = 0; i < AES_BLOCKLEN; i++) {
      vptr_enc[i] = 0;
    }
  }
  buffer[decryptedLen] = '\0';

  // Send password
  Keyboard.print(buffer);

  // Security: Clear buffer multiple times using volatile pointer
  volatile char* vptr = (volatile char*)buffer;
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < (int)sizeof(buffer); i++) {
      vptr[i] = (pass == 0) ? 0xFF : ((pass == 1) ? 0xAA : 0x00);
    }
  }
  
  // Clear AES context
  volatile byte* ctx_ptr = (volatile byte*)&ctx;
  for (int i = 0; i < (int)sizeof(ctx); i++) {
    ctx_ptr[i] = 0;
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

  // Check after each press if a combination matches
  int matchIndex = findMatchingPassword(currentSequenceIndex);
  if (matchIndex >= 0) {
    // Match found!
    executePassword(matchIndex);
    blinkSuccess(4);

    // Reset brute-force counter on success
    failedAttempts = 0;
    saveFailedAttempts();

    // Reset for next sequence
    currentSequenceIndex = 0;
    memset(currentInput, 0, sizeof(currentInput));
    return;
  }

  // Check if current input could lead to any valid combination
  bool couldMatch = false;
  for (int i = 0; i < PASSWORD_ENTRY_COUNT; i++) {
    // Load entry from PROGMEM to RAM
    PasswordEntry entry;
    memcpy_P(&entry, &PASSWORD_ENTRIES[i], sizeof(PasswordEntry));
    int seqLen = entry.sequence_len;

    // If our input is longer than this sequence, it can never match
    if (currentSequenceIndex > seqLen) {
      continue;
    }

    // Check if current input matches the start of this sequence
    bool matchesStart = true;
    for (int j = 0; j < currentSequenceIndex; j++) {
      byte expectedByte = pgm_read_byte(&entry.sequence[j]);
      if (currentInput[j] != expectedByte) {
        matchesStart = false;
        break;
      }
    }

    if (matchesStart) {
      couldMatch = true;
      break;
    }
  }

  if (!couldMatch) {
    // This input cannot lead to any valid combination
    failedAttempts++;
    saveFailedAttempts();

    if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
      // Activate lockout
      isLockedOut = true;
      lockoutStart = millis();
      blinkLockout();
    } else {
      blinkFail();
    }

    currentSequenceIndex = 0;
    memset(currentInput, 0, sizeof(currentInput));
  }
}

// ==================== Setup & Main Loop ====================
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  
  Keyboard.begin();
  
  // Initialize device ID and load persistent state
  initializeDeviceID();
  
  // Derive encryption key from master key + device ID
  deriveEncryptionKey();
  
  currentSequenceIndex = 0;
  lastAction = millis();
  
  // Check if we're starting in lockout state
  if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
    isLockedOut = true;
    lockoutStart = millis();
  }
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

  // Timeout: no input for too long -> Reset
  if (currentSequenceIndex > 0 && (unsigned long)(now - lastAction) > TIMEOUT_MS) {
    blinkFail();
    currentSequenceIndex = 0;
    memset(currentInput, 0, sizeof(currentInput));
    buttonWasPressed = false;  // Reset button state to prevent race condition
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
