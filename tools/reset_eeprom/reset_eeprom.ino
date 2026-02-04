// EEPROM Reset Tool
// Flash this sketch to reset EEPROM and force stage-2 re-encryption
// After flashing this, flash the main sketch again

#include <EEPROM.h>

#define EEPROM_REENCRYPT_DONE_ADDR 18

void setup() {
  // Initialize LED
  pinMode(10, OUTPUT);
  
  // Clear re-encryption flag
  EEPROM.write(EEPROM_REENCRYPT_DONE_ADDR, 0x00);
  
  // Blink to indicate done
  for (int i = 0; i < 10; i++) {
    digitalWrite(10, HIGH);
    delay(100);
    digitalWrite(10, LOW);
    delay(100);
  }
}

void loop() {
  // Stay in loop blinking slowly
  digitalWrite(10, HIGH);
  delay(1000);
  digitalWrite(10, LOW);
  delay(1000);
}
