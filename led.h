// Non-blocking LED control module
// Provides status feedback without using delay()

#ifndef LED_H
#define LED_H

#include <Arduino.h>

enum LedPattern {
  LED_OFF,
  LED_SUCCESS,    // 4 quick blinks
  LED_FAIL,       // Solid for 2 seconds
  LED_LOCKOUT     // 10 very fast blinks
};

class Led {
private:
  uint8_t pin;
  LedPattern currentPattern;
  unsigned long patternStartTime;
  int blinkCount;
  bool ledState;
  
public:
  Led(uint8_t ledPin) : pin(ledPin), currentPattern(LED_OFF), 
                        patternStartTime(0), blinkCount(0), ledState(false) {}
  
  void begin() {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  
  /**
   * Start a LED pattern
   */
  void setPattern(LedPattern pattern) {
    currentPattern = pattern;
    patternStartTime = millis();
    blinkCount = 0;
    ledState = false;
    digitalWrite(pin, LOW);
  }
  
  /**
   * Update LED state (non-blocking)
   * Call this in your main loop
   */
  void update() {
    unsigned long now = millis();
    unsigned long elapsed = now - patternStartTime;
    
    switch (currentPattern) {
      case LED_OFF:
        if (ledState) {
          digitalWrite(pin, LOW);
          ledState = false;
        }
        break;
        
      case LED_SUCCESS:
        // 4 quick blinks: 100ms ON, 100ms OFF
        if (blinkCount < 4) {
          int cycleTime = elapsed % 200;
          bool shouldBeOn = (cycleTime < 100);
          
          if (shouldBeOn != ledState) {
            digitalWrite(pin, shouldBeOn ? HIGH : LOW);
            ledState = shouldBeOn;
          }
          
          if (elapsed >= (blinkCount + 1) * 200) {
            blinkCount++;
          }
        } else {
          currentPattern = LED_OFF;
          digitalWrite(pin, LOW);
          ledState = false;
        }
        break;
        
      case LED_FAIL:
        // Solid for 2 seconds
        if (elapsed < 2000) {
          if (!ledState) {
            digitalWrite(pin, HIGH);
            ledState = true;
          }
        } else {
          currentPattern = LED_OFF;
          digitalWrite(pin, LOW);
          ledState = false;
        }
        break;
        
      case LED_LOCKOUT:
        // 10 very fast blinks: 50ms ON, 50ms OFF
        if (blinkCount < 10) {
          int cycleTime = elapsed % 100;
          bool shouldBeOn = (cycleTime < 50);
          
          if (shouldBeOn != ledState) {
            digitalWrite(pin, shouldBeOn ? HIGH : LOW);
            ledState = shouldBeOn;
          }
          
          if (elapsed >= (blinkCount + 1) * 100) {
            blinkCount++;
          }
        } else {
          currentPattern = LED_OFF;
          digitalWrite(pin, LOW);
          ledState = false;
        }
        break;
    }
  }
  
  bool isActive() {
    return currentPattern != LED_OFF;
  }
};

#endif
