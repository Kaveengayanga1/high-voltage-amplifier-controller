// Black Pill health check — blinks onboard LED (PC13, active LOW)
// Also prints to Serial so you can verify USB/UART works.
void setup() {
  pinMode(PC13, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  digitalWrite(PC13, LOW);   // LED ON (active low)
  Serial.println("LED ON");
  delay(500);
  digitalWrite(PC13, HIGH);  // LED OFF
  Serial.println("LED OFF");
  delay(500);
}