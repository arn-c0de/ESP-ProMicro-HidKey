// HID Tastatur mit Multi-Passwort-System
// Verschiedene Tastenseqnenzen schalten verschiedene Passwörter
// Pro Micro Leonardo (ATmega32U4)

#include <Keyboard.h>
#include "embedded_passwords.h"

#define LED_PIN 10
#define BUTTON_PIN 9
#define LONG_PRESS_MS 500
#define TIMEOUT_MS 3000

// Brute-Force-Schutz
#define MAX_FAILED_ATTEMPTS 5
#define LOCKOUT_MS 30000  // 30 Sekunden Sperre

// State
int currentSequenceIndex = 0;
unsigned long pressStart = 0;
unsigned long lastAction = 0;
bool buttonWasPressed = false;

// Brute-Force State
int failedAttempts = 0;
unsigned long lockoutStart = 0;
bool isLockedOut = false;

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

void blinkLockout() {
  // Schnelles Blinken zeigt Lockout an
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
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

  // XOR-Dekodierung des Passworts
  char buffer[64];
  int len = min(entry.password_len, (int)sizeof(buffer) - 1);

  for (int i = 0; i < len; i++) {
    byte encoded = pgm_read_byte(&entry.password[i]);
    buffer[i] = encoded ^ XOR_KEY;
  }
  buffer[len] = '\0';

  // Passwort senden
  Keyboard.print(buffer);

  // Sicherheit: Buffer sofort löschen
  memset(buffer, 0, sizeof(buffer));
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

    // Reset Brute-Force-Counter bei Erfolg
    failedAttempts = 0;

    // Reset für nächste Sequenz
    currentSequenceIndex = 0;
    memset(currentInput, 0, sizeof(currentInput));
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
    failedAttempts++;

    if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
      // Lockout aktivieren
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
  
  currentSequenceIndex = 0;
  lastAction = millis();
}

void loop() {
  bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  // Lockout-Prüfung
  if (isLockedOut) {
    if ((now - lockoutStart) >= LOCKOUT_MS) {
      // Lockout beenden
      isLockedOut = false;
      failedAttempts = 0;
      blinkSuccess(2);  // Kurzes Signal: wieder bereit
    } else {
      // Noch gesperrt - ignoriere Eingaben
      delay(10);
      return;
    }
  }

  // Timeout: zu lange keine Eingabe -> Reset
  if (currentSequenceIndex > 0 && (now - lastAction) > TIMEOUT_MS) {
    blinkFail();
    currentSequenceIndex = 0;
    memset(currentInput, 0, sizeof(currentInput));
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
