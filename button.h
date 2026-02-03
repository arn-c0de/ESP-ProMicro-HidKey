// Button handling module
// Provides debounced button reading with short/long press detection

#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

#define LONG_PRESS_THRESHOLD_MS 500
#define DEBOUNCE_DELAY_MS 50

class Button {
private:
  uint8_t pin;
  unsigned long pressStartTime;
  unsigned long lastDebounceTime;
  bool lastButtonState;
  bool buttonPressed;
  bool pressProcessed;
  
public:
  Button(uint8_t buttonPin) : pin(buttonPin), pressStartTime(0), 
                               lastDebounceTime(0), lastButtonState(HIGH),
                               buttonPressed(false), pressProcessed(false) {}
  
  void begin() {
    pinMode(pin, INPUT_PULLUP);
    lastButtonState = digitalRead(pin);
  }
  
  /**
   * Update button state with debouncing
   * Call this in your main loop
   */
  void update() {
    bool currentState = digitalRead(pin);
    
    // Debouncing logic
    if (currentState != lastButtonState) {
      lastDebounceTime = millis();
    }
    
    if ((unsigned long)(millis() - lastDebounceTime) > DEBOUNCE_DELAY_MS) {
      // Button state has stabilized
      if (currentState == LOW && !buttonPressed) {
        // Button just pressed (active LOW with pullup)
        buttonPressed = true;
        pressStartTime = millis();
        pressProcessed = false;
      }
      else if (currentState == HIGH && buttonPressed) {
        // Button released
        buttonPressed = false;
      }
    }
    
    lastButtonState = currentState;
  }
  
  /**
   * Check if button was released and return press type
   * @return -1 if no press, 0 for short press, 1 for long press
   */
  int getPress() {
    if (!buttonPressed && !pressProcessed && pressStartTime > 0) {
      // Button was released
      pressProcessed = true;
      unsigned long pressDuration = millis() - pressStartTime;
      pressStartTime = 0;
      
      return (pressDuration >= LONG_PRESS_THRESHOLD_MS) ? 1 : 0;
    }
    return -1;
  }
  
  bool isPressed() {
    return buttonPressed;
  }
  
  void reset() {
    buttonPressed = false;
    pressProcessed = true;
    pressStartTime = 0;
  }
};

#endif
