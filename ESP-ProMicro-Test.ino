// HID Tastatur mit Geheimcode-Button
// Sequenz: kurz, kurz, lang, kurz = "test" ausgeben
// Pro Micro Leonardo (ATmega32U4)

#include <Keyboard.h>

#define LED_PIN 10
#define BUTTON_PIN 9
#define LONG_PRESS_MS 500
#define TIMEOUT_MS 3000

// Erwartete Sequenz: 0=kurz, 1=lang
// kurz, kurz, lang, kurz
const int SEQUENCE[] = {0, 0, 1, 0};
const int SEQUENCE_LEN = 4;

int sequenceIndex = 0;
unsigned long pressStart = 0;
unsigned long lastAction = 0;
bool buttonWasPressed = false;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  Keyboard.begin();
}

void blinkSuccess() {
  for (int i = 0; i < 4; i++) {
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

void resetSequence() {
  sequenceIndex = 0;
}

void loop() {
  bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  // Timeout: zu lange keine Eingabe -> Reset
  if (sequenceIndex > 0 && (now - lastAction) > TIMEOUT_MS) {
    blinkFail();
    resetSequence();
  }

  // Button gerade gedrückt
  if (buttonPressed && !buttonWasPressed) {
    pressStart = now;
    buttonWasPressed = true;
  }

  // Button losgelassen
  if (!buttonPressed && buttonWasPressed) {
    unsigned long pressDuration = now - pressStart;
    int pressType = (pressDuration >= LONG_PRESS_MS) ? 1 : 0;

    lastAction = now;
    buttonWasPressed = false;

    // Prüfe ob Eingabe korrekt
    if (pressType == SEQUENCE[sequenceIndex]) {
      sequenceIndex++;

      // Sequenz komplett?
      if (sequenceIndex >= SEQUENCE_LEN) {
        Keyboard.print("test");
        blinkSuccess();
        resetSequence();
      }
    } else {
      // Falsche Eingabe
      blinkFail();
      resetSequence();
    }
  }

  delay(10);
}
