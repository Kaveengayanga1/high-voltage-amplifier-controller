// #include <MCUFRIEND_kbv.h>
// #include <Adafruit_GFX.h>

// MCUFRIEND_kbv tft;

// // Color definitions (RGB565)
// #define BLACK   0x0000
// #define WHITE   0xFFFF
// #define RED     0xF800
// #define GREEN   0x07E0
// #define BLUE    0x001F
// #define YELLOW  0xFFE0
// #define CYAN    0x07FF
// #define GREY    0x7BEF

// float voltage = 0.0;
// float temperature = 0.0;
// float current = 0.0;

// void setup() {
//   Serial.begin(9600);
//   uint16_t ID = tft.readID();
//   Serial.print("Display ID: 0x");
//   Serial.println(ID, HEX);

//   // Force ID if needed (uncomment if diagnose showed an odd value)
//   // if (ID == 0xD3D3) ID = 0x9486;

//   tft.begin(ID);
//   tft.setRotation(1);  // 1 = landscape (480 wide, 320 tall)
//   tft.fillScreen(BLACK);

//   drawStaticUI();
// }

// void loop() {
//   // Replace with real sensor readings
//   voltage     = 3.30 + (random(-20, 20) / 100.0);
//   temperature = 25.0 + (random(-30, 30) / 10.0);
//   current     = 0.50 + (random(-10, 10) / 100.0);

//   updateValues();
//   delay(500);
// }

// void drawStaticUI() {
//   // Header
//   tft.fillRect(0, 0, 480, 40, BLUE);
//   tft.setTextColor(WHITE);
//   tft.setTextSize(3);
//   tft.setCursor(10, 10);
//   tft.print("Circuit Monitor");

//   // Labels
//   tft.setTextSize(2);
//   tft.setTextColor(CYAN);

//   tft.setCursor(20, 70);   tft.print("Voltage:");
//   tft.setCursor(20, 150);  tft.print("Temperature:");
//   tft.setCursor(20, 230);  tft.print("Current:");

//   // Dividers
//   tft.drawFastHLine(0, 130, 480, GREY);
//   tft.drawFastHLine(0, 210, 480, GREY);
// }

// void updateValues() {
//   tft.setTextSize(4);
//   tft.setTextColor(WHITE, BLACK);  // BLACK background overwrites old value

//   // Voltage
//   tft.setCursor(250, 75);
//   tft.print(voltage, 2);
//   tft.print(" V  ");

//   // Temperature
//   tft.setCursor(250, 155);
//   tft.print(temperature, 1);
//   tft.print(" C  ");

//   // Current
//   tft.setCursor(250, 235);
//   tft.print(current, 2);
//   tft.print(" A  ");
// }

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Starting TFT init...");

  tft.init();
  tft.setRotation(1);              // 1 = landscape 480x320
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20, 20);
  tft.println("Hello Black Pill!");

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(20, 70);
  tft.println("ILI9488 480x320");

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(20, 120);
  tft.println("8-bit Parallel");

  // Draw some shapes to verify
  tft.fillRect(20, 180, 100, 60, TFT_RED);
  tft.fillRect(140, 180, 100, 60, TFT_GREEN);
  tft.fillRect(260, 180, 100, 60, TFT_BLUE);
  tft.drawRect(20, 260, 340, 40, TFT_YELLOW);

  Serial.println("TFT init complete");
}

void loop() {
  // Blink a status indicator to confirm alive
  static uint32_t last = 0;
  static bool state = false;
  if (millis() - last > 500) {
    last = millis();
    state = !state;
    tft.fillCircle(450, 20, 10, state ? TFT_GREEN : TFT_BLACK);
  }
}