// Test: LED durchgängig an (solid light)
const int LED_PIN = 10;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED immer an
}

void loop() {
  // LED bleibt an
}
