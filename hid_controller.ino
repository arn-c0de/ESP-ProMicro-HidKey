// ╔══════════════════════════════════════════════════════════════════╗
// ║  HID Controller Module - USB Keyboard Emulation                 ║
// ║  Secure password output with anti-timing randomization          ║
// ╚══════════════════════════════════════════════════════════════════╝

// ===== SEND PASSWORD VIA USB HID =====
// Types out password character-by-character with slight delays
// Adds randomization to prevent timing analysis
void sendPasswordViaHID(const char* password) {
  // Input validation
  if (password == NULL || password[0] == '\0') {
    return;  // Nothing to send
  }
  
  // Length validation (max 63 chars + null terminator)
  size_t len = strnlen(password, 64);
  if (len == 0 || len > 63) {
    return;  // Invalid length
  }
  
  // Ensure Keyboard is ready and USB HID has focus
  // This delay gives the target system time to recognize input
  delay(500);
  
  // Re-initialize keyboard to ensure it's active
  Keyboard.begin();
  delay(100);
  
  // Type each character
  for (size_t i = 0; i < len; i++) {
    // Send character via USB HID
    Keyboard.write(password[i]);
    
    // Add small random delay (anti-timing)
    // Prevents timing-based analysis of password length
    uint8_t randomDelay = 30 + (micros() % 40);  // 30-70ms random
    delay(randomDelay);
  }
  
  // Press Enter key to submit
  Keyboard.write(KEY_RETURN);
  
  // Brief delay before releasing control
  delay(100);
}

// ===== SEND RAW KEY CODE =====
// Sends a raw keyboard code (for special keys)
void sendKeyCode(uint8_t keyCode) {
  Keyboard.press(keyCode);
  delay(50);
  Keyboard.release(keyCode);
}

// ===== SEND KEY COMBINATION =====
// Sends a modifier + key combination (e.g., Ctrl+C)
void sendKeyCombo(uint8_t modifier, uint8_t key) {
  Keyboard.press(modifier);
  delay(50);
  Keyboard.press(key);
  delay(50);
  Keyboard.release(key);
  Keyboard.release(modifier);
  delay(50);
}

// ===== TYPE STRING =====
// Types a string without Enter key
void typeString(const char* str) {
  if (str == NULL) return;
  
  Keyboard.print(str);
}
