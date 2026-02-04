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
#define EEPROM_REENCRYPT_DONE_ADDR 18  // Flag: Stage-2 re-encryption completed (18)
#define EEPROM_PASSWORDS_START 32      // Start of re-encrypted passwords storage (32+)

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

// Runtime storage for stage-2 encrypted passwords (loaded from EEPROM)
// Max: 3 passwords * 64 bytes each = 192 bytes
#define MAX_PASSWORD_BLOCK_SIZE 64
#define MAX_PASSWORDS 10
byte stage2Passwords[MAX_PASSWORDS][MAX_PASSWORD_BLOCK_SIZE];
int stage2PasswordLengths[MAX_PASSWORDS];
int stage2PlaintextLengths[MAX_PASSWORDS];
bool stage2Ready = false;

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
  // Derive device-specific key: Master Key XOR Device ID
  byte masterKey[16];
  byte deviceID[16];
  
  // Read master key from PROGMEM
  for (int i = 0; i < 16; i++) {
    masterKey[i] = pgm_read_byte(&AES_MASTER_KEY[i]);
  }
  
  // Read device ID from EEPROM
  for (int i = 0; i < 16; i++) {
    deviceID[i] = EEPROM.read(EEPROM_DEVICE_ID_ADDR + i);
  }
  
  // XOR to create device-specific key
  for (int i = 0; i < 16; i++) {
    derivedKey[i] = masterKey[i] ^ deviceID[i];
  }
  
  // Clear sensitive data
  memset(masterKey, 0, 16);
  memset(deviceID, 0, 16);
}

// ==================== Stage-2 Re-Encryption ====================
/**
 * Performs stage-2 re-encryption on first boot:
 * 1. Decrypt passwords with master key (stage-1)
 * 2. Re-encrypt with device-specific key (stage-2)
 * 3. Store in EEPROM for future use
 */
void performStage2ReEncryption() {
  digitalWrite(LED_PIN, HIGH);  // Visual feedback during re-encryption
  
  byte masterKey[16];
  for (int i = 0; i < 16; i++) {
    masterKey[i] = pgm_read_byte(&AES_MASTER_KEY[i]);
  }
  
  AES_ctx masterCtx, deviceCtx;
  AES_init_ctx(&masterCtx, masterKey);
  AES_init_ctx(&deviceCtx, derivedKey);
  
  int eepromOffset = EEPROM_PASSWORDS_START;
  
  for (int i = 0; i < PASSWORD_ENTRY_COUNT; i++) {
    PasswordEntry entry;
    memcpy_P(&entry, &PASSWORD_ENTRIES[i], sizeof(PasswordEntry));
    
    byte encBuffer[MAX_PASSWORD_BLOCK_SIZE];
    int encLen = entry.password_len;
    
    // Read stage-1 encrypted password from PROGMEM
    for (int j = 0; j < encLen; j++) {
      encBuffer[j] = pgm_read_byte(&entry.password[j]);
    }
    
    // Check stage-1 flag (first byte should be 0x01)
    if (encBuffer[0] != ENCRYPTION_STAGE_1) {
      // Invalid format, skip
      continue;
    }
    
    // Remove flag byte, shift data
    encLen--;
    for (int j = 0; j < encLen; j++) {
      encBuffer[j] = encBuffer[j + 1];
    }
    
    // Stage-1 Decrypt: Use master key to decrypt
    int blocks = (encLen + 15) / 16;
    for (int b = 0; b < blocks; b++) {
      AES_ECB_decrypt(&masterCtx, encBuffer + (b * 16));
    }
    
    // Now we have plaintext - re-encrypt with device key using XOR
    int plaintextLen = entry.plaintext_len;
    
    // Pad to 16-byte blocks
    int paddedLen = ((plaintextLen + 15) / 16) * 16;
    for (int j = plaintextLen; j < paddedLen; j++) {
      encBuffer[j] = 0;
    }
    
    // Stage-2 Encrypt: XOR with device-specific key
    for (int j = 0; j < paddedLen; j++) {
      encBuffer[j] ^= derivedKey[j % 16];
    }
    
    // Add stage-2 flag
    byte finalBuffer[MAX_PASSWORD_BLOCK_SIZE];
    finalBuffer[0] = ENCRYPTION_STAGE_2;
    for (int j = 0; j < paddedLen && j < MAX_PASSWORD_BLOCK_SIZE - 1; j++) {
      finalBuffer[j + 1] = encBuffer[j];
    }
    
    int finalLen = paddedLen + 1;
    
    // Store in EEPROM
    for (int j = 0; j < finalLen; j++) {
      EEPROM.write(eepromOffset + j, finalBuffer[j]);
    }
    
    // Load into RAM for runtime use
    stage2PasswordLengths[i] = finalLen;
    stage2PlaintextLengths[i] = plaintextLen;
    memcpy(stage2Passwords[i], finalBuffer, finalLen);
    
    eepromOffset += finalLen;
    
    // Security: Clear buffers
    memset(encBuffer, 0, sizeof(encBuffer));
    memset(finalBuffer, 0, sizeof(finalBuffer));
  }
  
  // Mark re-encryption as done
  EEPROM.write(EEPROM_REENCRYPT_DONE_ADDR, 0xEE);
  stage2Ready = true;
  
  // Clear sensitive data
  memset(masterKey, 0, 16);
  memset(&masterCtx, 0, sizeof(masterCtx));
  memset(&deviceCtx, 0, sizeof(deviceCtx));
  
  digitalWrite(LED_PIN, LOW);
}

/**
 * Load stage-2 encrypted passwords from EEPROM into RAM
 */
void loadStage2Passwords() {
  int eepromOffset = EEPROM_PASSWORDS_START;
  
  for (int i = 0; i < PASSWORD_ENTRY_COUNT; i++) {
    PasswordEntry entry;
    memcpy_P(&entry, &PASSWORD_ENTRIES[i], sizeof(PasswordEntry));
    
    // Calculate expected length based on plaintext
    int plaintextLen = entry.plaintext_len;
    int paddedLen = ((plaintextLen + 15) / 16) * 16;
    int finalLen = paddedLen + 1;  // +1 for flag
    
    // Load from EEPROM
    for (int j = 0; j < finalLen && j < MAX_PASSWORD_BLOCK_SIZE; j++) {
      stage2Passwords[i][j] = EEPROM.read(eepromOffset + j);
    }
    
    stage2PasswordLengths[i] = finalLen;
    stage2PlaintextLengths[i] = plaintextLen;
    eepromOffset += finalLen;
  }
  
  stage2Ready = true;
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
  
  if (!stage2Ready) {
    // Stage-2 not ready, cannot execute
    blinkFail();
    return;
  }

  // Use stage-2 encrypted password from RAM
  byte decBuffer[MAX_PASSWORD_BLOCK_SIZE];
  int encLen = stage2PasswordLengths[entryIndex];
  int plaintextLen = stage2PlaintextLengths[entryIndex];
  
  memcpy(decBuffer, stage2Passwords[entryIndex], encLen);
  
  // Check stage-2 flag
  if (decBuffer[0] != ENCRYPTION_STAGE_2) {
    blinkFail();
    memset(decBuffer, 0, sizeof(decBuffer));
    return;
  }
  
  // Remove flag
  encLen--;
  for (int i = 0; i < encLen; i++) {
    decBuffer[i] = decBuffer[i + 1];
  }
  
  // Decrypt with device-specific key (XOR)
  for (int i = 0; i < encLen; i++) {
    decBuffer[i] ^= derivedKey[i % 16];
  }
  
  // Type password
  Keyboard.begin();
  for (int i = 0; i < plaintextLen; i++) {
    Keyboard.write(decBuffer[i]);
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
  
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  
  Keyboard.begin();
  
  // Initialize device ID if first boot
  initializeDeviceID();
  
  // Derive device-specific encryption key
  deriveEncryptionKey();
  
  // Check if stage-2 re-encryption was already performed
  byte reencryptDone = EEPROM.read(EEPROM_REENCRYPT_DONE_ADDR);
  
  if (reencryptDone != 0xEE) {
    // First boot or EEPROM was cleared: perform stage-2 re-encryption
    performStage2ReEncryption();
  } else {
    // Load existing stage-2 passwords from EEPROM
    loadStage2Passwords();
  }
  
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
