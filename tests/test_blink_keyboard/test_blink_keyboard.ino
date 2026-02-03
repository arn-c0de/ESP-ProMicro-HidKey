#include <Keyboard.h>

const int LED_PIN = 10;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Allow host to enumerate USB
  Keyboard.begin();
  delay(1500);

  // Send a short HID string to confirm keyboard enumeration
  Keyboard.println("HID TEST - OK");

  // Blink to show sketch is running
  for (int i = 0; i < 3; ++i) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(1500);
}
