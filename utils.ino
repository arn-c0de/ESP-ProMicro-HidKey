// ╔══════════════════════════════════════════════════════════════════╗
// ║  Utility Functions - LED Control & Helpers                      ║
// ╚══════════════════════════════════════════════════════════════════╝

// ===== BLINK LED =====
// Blinks the LED a specified number of times
// Used for visual feedback (no side-channel info during sensitive ops)
void blinkLED(int count, int delayMs) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

// ===== SET LED STATE =====
// Sets LED on or off
void setLED(bool state) {
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}

// ===== PULSE LED =====
// Creates a fading pulse effect (for bootloader indication)
void pulseLED(int duration) {
  unsigned long startTime = millis();
  while (millis() - startTime < duration) {
    // Fade in
    for (int i = 0; i < 255; i += 5) {
      analogWrite(LED_PIN, i);
      delay(5);
    }
    // Fade out
    for (int i = 255; i >= 0; i -= 5) {
      analogWrite(LED_PIN, i);
      delay(5);
    }
  }
  digitalWrite(LED_PIN, LOW);
}

// ===== RANDOM DELAY =====
// Adds random timing jitter (anti-timing attack)
void randomDelay(int minMs, int maxMs) {
  int delayTime = minMs + (micros() % (maxMs - minMs + 1));
  delay(delayTime);
}

// ===== SECURE MEMORY COMPARE =====
// Constant-time memory comparison (prevents timing attacks)
bool secureMemCompare(const void* a, const void* b, size_t len) {
  const uint8_t* pa = (const uint8_t*)a;
  const uint8_t* pb = (const uint8_t*)b;
  uint8_t diff = 0;
  
  for (size_t i = 0; i < len; i++) {
    diff |= pa[i] ^ pb[i];
  }
  
  return (diff == 0);
}
