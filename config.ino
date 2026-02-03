// ╔══════════════════════════════════════════════════════════════════╗
// ║  Configuration Module - Device Settings                         ║
// ╚══════════════════════════════════════════════════════════════════╝

// ===== DEVICE IDENTIFICATION =====
const char* DEVICE_NAME = "SecureHID_ProMicro";
const char* DEVICE_VERSION = "1.0.0";
const char* BUILD_DATE = __DATE__;
const char* BUILD_TIME = __TIME__;

// ===== SECURITY SETTINGS =====
const int SECURITY_MAX_FAIL_ATTEMPTS = 3;
const bool SECURITY_AUTO_LOCK = true;
const bool SECURITY_WIPE_RAM = true;

// ===== TIMING SETTINGS =====
const unsigned long TIMING_LONG_PRESS_MS = 500;
const unsigned long TIMING_INPUT_WINDOW_MS = 5000;
const unsigned long TIMING_HID_DELAY_MS = 400;

// ===== DEBUG SETTINGS =====
// WARNING: Never enable in production (leaks sensitive info)
#ifdef DEBUG_MODE
const bool DEBUG_ENABLED = true;
const bool DEBUG_SERIAL_OUTPUT = true;
#else
const bool DEBUG_ENABLED = false;
const bool DEBUG_SERIAL_OUTPUT = false;
#endif

// ===== LED FEEDBACK PATTERNS =====
const int LED_PATTERN_BOOT = 2;           // 2 blinks on startup
const int LED_PATTERN_INPUT = 1;          // 1 blink per button press
const int LED_PATTERN_UNLOCK = 5;         // 5 blinks on unlock success
const int LED_PATTERN_ERROR = 3;          // 3 blinks on error
const int LED_PATTERN_LOCKED = -1;        // Solid on when locked

// ===== FEATURE FLAGS =====
const bool FEATURE_FAKE_PASSWORD = true;
const bool FEATURE_MULTI_PASSWORD = true;
const bool FEATURE_AUTO_ENTER = true;      // Auto-press Enter after password
const bool FEATURE_ANTI_TIMING = true;     // Randomized delays

// ===== HARDWARE CONFIGURATION =====
// Can be changed for different pin configurations
const int HW_BUTTON_PIN = 9;
const int HW_LED_PIN = 10;
const bool HW_BUTTON_PULLUP = true;
const bool HW_LED_ACTIVE_HIGH = true;

// ===== GET DEVICE INFO =====
void printDeviceInfo() {
  #ifdef DEBUG_MODE
  Serial.begin(9600);
  delay(1000);
  Serial.println("===== SECURE HID PRO MICRO =====");
  Serial.print("Device:  ");
  Serial.println(DEVICE_NAME);
  Serial.print("Version: ");
  Serial.println(DEVICE_VERSION);
  Serial.print("Built:   ");
  Serial.print(BUILD_DATE);
  Serial.print(" ");
  Serial.println(BUILD_TIME);
  Serial.println("================================");
  Serial.println("WARNING: Debug mode enabled!");
  Serial.println("Disable DEBUG_MODE in production");
  Serial.println("================================");
  #endif
}
