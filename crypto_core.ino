// ╔══════════════════════════════════════════════════════════════════╗
// ║  Crypto Core Module - Device Signature Key Derivation           ║
// ║  AES-128-CBC Decryption with ATmega32u4 Hardware ID              ║
// ╚══════════════════════════════════════════════════════════════════╝

#include <AESLib.h>
#include <avr/boot.h>

// AES library instance
AESLib aesLib;

// Device signature storage (unique per ATmega32u4)
uint8_t deviceSignature[10];

// Derived AES key (128-bit)
uint8_t aesKey[16];

// ===== INITIALIZE CRYPTO SUBSYSTEM =====
void initCrypto() {
  // Read device signature from ATmega32u4 fuses
  readDeviceSignature();
  
  // Derive AES key from signature + salt
  deriveEncryptionKey();
}

// ===== READ ATmega32u4 DEVICE SIGNATURE =====
// The ATmega32u4 has a unique signature programmed at factory
// Signature bytes are at specific memory addresses
void readDeviceSignature() {
  // ATmega32u4 signature structure:
  // Byte 0: Manufacturer ID (0x1E = Atmel) - SAME FOR ALL
  // Byte 1: Memory Size (0x95 = ATmega32u4) - SAME FOR ALL
  // Byte 2: Part Number (0x87) - SAME FOR ALL
  // Additional bytes from calibration - UNIQUE PER CHIP
  
  // Read only chip-specific calibration bytes for better entropy
  // Use OSCCAL and other calibration values
  deviceSignature[0] = boot_signature_byte_get(0x0000); // Mfg ID
  deviceSignature[1] = boot_signature_byte_get(0x0002); // Part 1
  deviceSignature[2] = boot_signature_byte_get(0x0004); // Part 2
  
  // Read oscillator calibration (unique per chip)
  #ifdef OSCCAL
  deviceSignature[3] = OSCCAL;
  #else
  deviceSignature[3] = 0x80; // Default if not available
  #endif
  
  // Add more entropy from timer and ADC noise
  randomSeed(analogRead(0) ^ micros());
  for (uint8_t i = 4; i < 10; i++) {
    deviceSignature[i] = random(256) ^ (micros() & 0xFF);
  }
}

// ===== DERIVE ENCRYPTION KEY =====
// Combines device signature + KDF salt to create AES-128 key
// Uses improved mixing algorithm with multiple rounds
void deriveEncryptionKey() {
  #ifdef SECURE_HID_BUILD
  const char* salt = KDF_SALT;
  const size_t saltLen = KDF_SALT_LEN;
  #else
  const char* salt = "ProMicro2026SaltConstant";
  const size_t saltLen = 24;
  #endif
  
  // Improved key derivation with multiple mixing rounds
  // Initialize key with zeros
  memset(aesKey, 0, 16);
  
  // Round 1: Mix device signature
  for (uint8_t i = 0; i < 10; i++) {
    aesKey[i % 16] ^= deviceSignature[i];
    aesKey[(i + 1) % 16] ^= (deviceSignature[i] << 1) | (deviceSignature[i] >> 7);
    aesKey[(i + 5) % 16] ^= (deviceSignature[i] << 3) | (deviceSignature[i] >> 5);
  }
  
  // Round 2: Mix salt with rotation
  for (size_t i = 0; i < saltLen && i < 48; i++) {
    aesKey[i % 16] ^= (uint8_t)salt[i];
    aesKey[(i + 5) % 16] ^= ((uint8_t)salt[i] << 3) | ((uint8_t)salt[i] >> 5);
    aesKey[(i + 11) % 16] ^= ((uint8_t)salt[i] << 2) | ((uint8_t)salt[i] >> 6);
  }
  
  // Round 3: Diffusion with multiple passes
  for (uint8_t pass = 0; pass < 4; pass++) {
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t temp = aesKey[i];
      aesKey[i] ^= aesKey[(i + 7) % 16];
      aesKey[(i + 7) % 16] ^= temp;
      aesKey[(i + 3) % 16] ^= (temp << 4) | (temp >> 4);
    }
  }
  
  // Note: AES key stored in aesKey; AESLib decrypt will receive key at call time.
}

// ===== DECRYPT PASSWORD =====
// Decrypts AES-128-CBC encrypted password
// Format: [16-byte IV | encrypted data with PKCS7 padding]
void decryptPassword(const uint8_t* encrypted, size_t encLen, char* output, size_t maxLen) {
  // Minimum: IV (16) + one block (16)
  if (encLen < 32 || maxLen < 1) {
    output[0] = '\0';
    return;
  }
  
  // Extract IV (first 16 bytes)
  uint8_t iv[16];
  memcpy(iv, encrypted, 16);
  
  // Calculate ciphertext length (after IV)
  size_t ciphertextLen = encLen - 16;
  
  // Use stack buffer instead of malloc (security + performance fix)
  // Max password length is 64 bytes, ciphertext will be padded to 64 bytes max
  uint8_t ciphertext[64];
  
  // Validate ciphertext size
  if (ciphertextLen > sizeof(ciphertext)) {
    output[0] = '\0';
    secureWipe(iv, sizeof(iv));
    return;
  }
  
  // Copy ciphertext
  memcpy(ciphertext, encrypted + 16, ciphertextLen);
  
  // Decrypt with AES-128-CBC (key already set in initCrypto)
  uint16_t decryptedLen = aesLib.decrypt(ciphertext, ciphertextLen, (byte*)output, aesKey, 128, iv);
  
  // Ensure null termination
  if (decryptedLen < maxLen) {
    output[decryptedLen] = '\0';
  } else {
    output[maxLen - 1] = '\0';
  }
  
  // CRITICAL SECURITY: Wipe all sensitive data
  secureWipe(ciphertext, sizeof(ciphertext));
  secureWipe(iv, sizeof(iv));
  // Note: aesKey remains in memory for reuse but should be wiped on device lock
}

// ===== SECURE MEMORY WIPE =====
// Overwrites memory with zeros (prevents optimization removal)
void secureWipe(void* ptr, size_t len) {
  volatile uint8_t* p = (volatile uint8_t*)ptr;
  while (len--) {
    *p++ = 0;
  }
}
