// HID Tastatur mit Multi-Passwort-System
// Verschiedene Tastenseqnenzen schalten verschiedene Passwörter
// Pro Micro Leonardo (ATmega32U4)

#include <Keyboard.h>
#include "embedded_passwords.h"

#define LED_PIN 10
#define BUTTON_PIN 9
#define LONG_PRESS_MS 500
#define TIMEOUT_MS 3000

// State
int currentSequenceIndex = 0;
unsigned long pressStart = 0;
unsigned long lastAction = 0;
bool buttonWasPressed = false;

// Buffer für aktuelle Eingabe-Sequenz
byte currentInput[20];  // Max 20 Tasten für längste Sequenz

// ==================== LED-Feedback ====================
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

// ==================== Sequenz-Matching ====================
/**
 * Prüft, ob currentInput die Sequenz aus PASSWORD_ENTRIES[entryIndex] matched
 * @param entryIndex Index im PASSWORD_ENTRIES Array
 * @param inputLength Länge der aktuellen Eingabe
 * @return true wenn vollständig gematched, false sonst
 */
bool sequenceMatches(int entryIndex, int inputLength) {
  if (entryIndex < 0 || entryIndex >= PASSWORD_ENTRY_COUNT) {
    return false;
  }

  // Lade Sequenzdaten aus PROGMEM in RAM
  PasswordEntry entry;
  memcpy_P(&entry, &PASSWORD_ENTRIES[entryIndex], sizeof(PasswordEntry));
  int seqLen = entry.sequence_len;

  // Länge muss stimmen
  if (inputLength != seqLen) {
    return false;
  }

  // Prüfe jeden Eintrag (sequence pointer zeigt auf PROGMEM)
  for (int i = 0; i < seqLen; i++) {
    byte expectedByte = pgm_read_byte(&entry.sequence[i]);
    if (currentInput[i] != expectedByte) {
      return false;
    }
  }

  return true;
}

/**
 * Prüft alle registrierten Kombinationen
 * @param inputLength Länge der aktuellen Eingabe
 * @return Index der matchenden Kombination, oder -1 wenn keine matched
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
 * Sendet das Passwort für eine Kombination
 * @param entryIndex Index im PASSWORD_ENTRIES Array
 */
void executePassword(int entryIndex) {
  if (entryIndex < 0 || entryIndex >= PASSWORD_ENTRY_COUNT) {
    return;
  }

  // Lade Entry aus PROGMEM in RAM
  PasswordEntry entry;
  memcpy_P(&entry, &PASSWORD_ENTRIES[entryIndex], sizeof(PasswordEntry));

  // Passwort direkt senden (String liegt im RAM, nicht PROGMEM)
  Keyboard.print(entry.password);
}

/**
 * Registriert eine neue Button-Eingabe
 * @param pressType 0 für kurz (< 500ms), 1 für lang (>= 500ms)
 */
void processButtonPress(int pressType) {
  // Prüfe ob dieser Druck passt zur längsten möglichen Sequenz
  if (currentSequenceIndex >= 20) {
    // Buffer voll, verwerfe und starte neu
    currentSequenceIndex = 0;
  }

  currentInput[currentSequenceIndex] = pressType;
  currentSequenceIndex++;
  lastAction = millis();

  // Prüfe nach jedem Druck, ob eine Kombination matched
  int matchIndex = findMatchingPassword(currentSequenceIndex);
  if (matchIndex >= 0) {
    // Match gefunden!
    executePassword(matchIndex);
    blinkSuccess(4);
    
    // Reset für nächste Sequenz
    currentSequenceIndex = 0;
    return;
  }

  // Prüfe ob aktuelle Eingabe zu keiner möglichen Kombination führen kann
  bool couldMatch = false;
  for (int i = 0; i < PASSWORD_ENTRY_COUNT; i++) {
    // Lade Entry aus PROGMEM in RAM
    PasswordEntry entry;
    memcpy_P(&entry, &PASSWORD_ENTRIES[i], sizeof(PasswordEntry));
    int seqLen = entry.sequence_len;

    // Wenn unsere bisherige Eingabe länger ist als diese Sequenz, kann sie nie matchen
    if (currentSequenceIndex > seqLen) {
      continue;
    }

    // Prüfe ob bisherige Eingabe mit Anfang dieser Sequenz matched
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
    // Diese Eingabe kann zu keiner gültigen Kombination führen
    blinkFail();
    currentSequenceIndex = 0;
  }
}

// ==================== Setup & Main Loop ====================
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  
  Keyboard.begin();
  
  currentSequenceIndex = 0;
  lastAction = millis();
}

void loop() {
  bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  // Timeout: zu lange keine Eingabe -> Reset
  if (currentSequenceIndex > 0 && (now - lastAction) > TIMEOUT_MS) {
    blinkFail();
    currentSequenceIndex = 0;
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

    buttonWasPressed = false;

    // Verarbeite den Button-Druck
    processButtonPress(pressType);
  }

  delay(10);
}
