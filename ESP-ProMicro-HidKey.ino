// HID Keyboard with Multi-Password System
// Different button sequences trigger different passwords
// Pro Micro Leonardo (ATmega32U4)

#include <Keyboard.h>
#include <EEPROM.h>
#include "embedded_passwords.h"
#include "aes.h"
#include "chacha20.h"
#include "sha256.h"
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
bool sequenceRecognized = false;         // Track if we're already processing a recognized sequence

// Brute-force state
int failedAttempts = 0;
unsigned long lockoutStart = 0;
bool isLockedOut = false;

// Buffer for current input sequence
byte currentInput[MAX_SEQUENCE_LENGTH];

// Device-specific encryption key (derived from master key + device ID)
byte derivedKey[CHACHA20_KEY_SIZE];  // 32 bytes for ChaCha20

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
  
  // First boot: Generate random device ID with improved entropy
  // Use multiple entropy sources for better randomness
  uint32_t seed = 0;
  for (int i = 0; i < 4; i++) {
    seed ^= analogRead(A0 + i);  // Multiple analog pins
    seed ^= micros();
    delay(10);
  }
  randomSeed(seed);
  
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
  // Derive device-specific key using HKDF-SHA256 instead of simple XOR
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
  
  // Combine master key and device ID as input key material
  byte ikm[32];
  memcpy(ikm, masterKey, 16);
  memcpy(ikm + 16, deviceID, 16);
  
  // Use HKDF to derive 32-byte key (for ChaCha20)
  const char* info = "ESP-ProMicro-HidKey-v2";
  HKDF_SHA256(derivedKey, CHACHA20_KEY_SIZE, ikm, 32, (const uint8_t*)info, strlen(info));
  
  // Clear sensitive data
  memset(masterKey, 0, 16);
  memset(deviceID, 0, 16);
  memset(ikm, 0, 32);
}


// ==================== Stage-2 Re-Encryption ====================
/**
 * Performs stage-2 re-encryption on first boot:
 * 1. Decrypt passwords with master key (stage-1, AES-CBC)
 * 2. Re-encrypt with device-specific key (stage-2, ChaCha20)
 * 3. Store in EEPROM for future use
 */
void performStage2ReEncryption() {
  digitalWrite(LED_PIN, HIGH);  // Visual feedback during re-encryption
  
  byte masterKey[16];
  for (int i = 0; i < 16; i++) {
    masterKey[i] = pgm_read_byte(&AES_MASTER_KEY[i]);
  }
  
  AES_ctx masterCtx;
  AES_init_ctx(&masterCtx, masterKey);
  
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
    
    // Next 16 bytes are IV, rest is ciphertext
    byte iv[16];
    memcpy(iv, encBuffer, 16);
    int cipherLen = encLen - 16;
    
    // Move ciphertext to beginning of buffer
    for (int j = 0; j < cipherLen; j++) {
      encBuffer[j] = encBuffer[j + 16];
    }
    
    // Stage-1 Decrypt: Use master key with CBC mode
    AES_CBC_decrypt(&masterCtx, iv, encBuffer, cipherLen);
    
    // Remove PKCS#7 padding
    // The last byte indicates how many padding bytes there are
    if (cipherLen > 0) {
      byte paddingLen = encBuffer[cipherLen - 1];
      if (paddingLen > 0 && paddingLen <= 16 && paddingLen <= cipherLen) {
        // Verify all padding bytes are correct
        bool validPadding = true;
        for (int j = cipherLen - paddingLen; j < cipherLen; j++) {
          if (encBuffer[j] != paddingLen) {
            validPadding = false;
            break;
          }
        }
        if (validPadding) {
          cipherLen -= paddingLen;
        }
      }
    }
    
    // Now we have plaintext - generate random nonce for ChaCha20
    byte nonce[CHACHA20_NONCE_SIZE];
    for (int j = 0; j < CHACHA20_NONCE_SIZE; j++) {
      nonce[j] = random(256);
    }
    
    // The actual plaintext length after unpadding
    int plaintextLen = cipherLen;
    
    // Stage-2 Encrypt: ChaCha20 with device-specific key
    ChaCha20_encrypt(encBuffer, plaintextLen, derivedKey, nonce, 0);
    
    // Prepare final buffer: flag + nonce + ciphertext
    byte finalBuffer[MAX_PASSWORD_BLOCK_SIZE];
    finalBuffer[0] = ENCRYPTION_STAGE_2;
    memcpy(finalBuffer + 1, nonce, CHACHA20_NONCE_SIZE);
    memcpy(finalBuffer + 1 + CHACHA20_NONCE_SIZE, encBuffer, plaintextLen);
    
    int finalLen = 1 + CHACHA20_NONCE_SIZE + plaintextLen;
    
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
    memset(nonce, 0, sizeof(nonce));
    memset(iv, 0, sizeof(iv));
  }
  
  // Mark re-encryption as done
  EEPROM.write(EEPROM_REENCRYPT_DONE_ADDR, 0xEE);
  stage2Ready = true;
  
  // Clear sensitive data
  memset(masterKey, 0, 16);
  memset(&masterCtx, 0, sizeof(masterCtx));
  
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
    
    // Calculate expected length: flag(1) + nonce(12) + ciphertext(plaintextLen)
    int plaintextLen = entry.plaintext_len;
    int finalLen = 1 + CHACHA20_NONCE_SIZE + plaintextLen;
    
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

// Character mapping removed — Keyboard is initialized with German layout (KeyboardLayout_de_DE) so raw bytes are sent directly.

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
  
  // Extract nonce (bytes 1-12)
  byte nonce[CHACHA20_NONCE_SIZE];
  memcpy(nonce, decBuffer + 1, CHACHA20_NONCE_SIZE);
  
  // Extract ciphertext (after flag and nonce)
  int cipherStart = 1 + CHACHA20_NONCE_SIZE;
  for (int i = 0; i < plaintextLen; i++) {
    decBuffer[i] = decBuffer[cipherStart + i];
  }
  
  // Decrypt with ChaCha20
  ChaCha20_decrypt(decBuffer, plaintextLen, derivedKey, nonce, 0);
  
  // Type password using German keyboard layout
  Keyboard.begin(KeyboardLayout_de_DE);
  for (int i = 0; i < plaintextLen; i++) {
    Keyboard.write(decBuffer[i]);
  }
  Keyboard.end();
  
  // Security: Multi-pass clear
  memset(decBuffer, 0xFF, sizeof(decBuffer));
  memset(decBuffer, 0xAA, sizeof(decBuffer));
  memset(decBuffer, 0x00, sizeof(decBuffer));
  memset(nonce, 0, sizeof(nonce));
  
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
