// Minimal ILI9488 test — new pin map (data PA0-PA7, WR PB13, RD PB14, CS PB9, DC PB10, RST PB12)
// No ADC, no Serial dependency. Expect: red / green / blue fills + text, 1s each.
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

void setup() {
  tft.init();
  tft.setRotation(1);
}

void loop() {
  tft.fillScreen(TFT_RED);    delay(1000);
  tft.fillScreen(TFT_GREEN);  delay(1000);
  tft.fillScreen(TFT_BLUE);   delay(1000);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("NEW PIN MAP OK", 240, 160, 4);
  delay(1500);
}
