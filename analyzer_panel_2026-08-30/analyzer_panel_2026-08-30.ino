// /*
//  * SIGNAL ANALYZER PANEL — rev 3.1 (UI card layout) — 2026-08-31
//  * STM32F411 Black Pill + 3.5" ILI9488 480x320 TFT shield (8-bit parallel)
//  *
//  * PIN MAP rev 3 (unchanged, matches WIRING.md and User_Setup.h 2026-08-30):
//  *   PA0        Signal input  (ADC1_IN0)
//  *   PA1        Thermistor    (ADC1_IN1)
//  *   PA2..PA5   GAIN mode inputs 1..4 (digital in, active HIGH by default)
//  *   PA6        Fan PWM (reserved)
//  *   PB0..PB7   LCD D0..D7
//  *   PB9  CS | PB10 DC | PB12 RST | PB13 WR | PB14 RD
//  *
//  * rev 3.1 UI: separate rounded-corner cards per parameter, no overlap,
//  * N/A fallbacks, units (Hz / V / °C), scope + waveform cards on the right.
//  */

// #include <TFT_eSPI.h>
// #include "analyzer_types.h"
// TFT_eSPI tft = TFT_eSPI();

// // ---------------- Pins (rev 3) ----------------
// #define SIG_PIN PA0
// #define THERM_PIN PA1
// const int GAIN_PINS[4] = { PA2, PA3, PA4, PA5 };
// #define GAIN_ACTIVE_HIGH 1  // set 0 if your gain lines are active LOW

// // ---------------- Input scaling ----------------
// #define VREF 3.3f
// #define INPUT_ATTEN 1.0f  // divider ratio for TRUE input volts: \
//                           // direct = 1.0f | 10k/10k = 2.0f | 68k/10k = 7.8f \
//                           // (classification is shape-based; ATTEN does not affect it)

// // ---------------- Thermistor — CONFIRM THESE ----------------
// #define THERM_R25 10000.0f
// #define THERM_BETA 3950.0f
// #define THERM_RSERIES 10000.0f
// #define T0_KELVIN 298.15f

// // ---------------- Sampling / classification ----------------
// #define NSAMP 512
// static uint16_t buf[NSAMP];
// #define MIN_VPP_COUNTS 80

// // Kurtosis (m4/m2^2) is the primary shape feature — averaged over all 512
// // samples, so single noisy ADC samples can't flip the class (unlike crest).
// // Theoretical: square 1.0 | sine 1.5 | triangle 1.8. Tune from Serial "kurt=".
// #define KURT_SQUARE_MAX 1.30f
// #define KURT_SINE_MAX 1.68f
// #define KURT_TRI_MAX 2.10f
// #define WAVE_CONFIRM_COUNT 3  // consecutive buffers to accept a new class
// // (crest-factor thresholds removed — crest kept in Features for debug only)

// const char* waveNames[] = { "NO SIGNAL", "SINE", "SQUARE", "TRIANGLE", "RAMP", "OTHER" };
// const uint16_t waveColors[] = { TFT_DARKGREY, TFT_CYAN, TFT_YELLOW, TFT_GREEN, TFT_ORANGE, TFT_RED };

// // ================= UI LAYOUT (480x320 landscape) =================
// // Header bar across the top; 4 stat cards in the left column;
// // scope card + waveform card in the right column. No region overlaps.
// #define HDR_H 28

// #define CARD_X 8  // left column
// #define CARD_W 230
// #define CARD_H 64
// #define CARD_GAP 6
// #define CARD_R 8                                           // corner radius
// #define CARD_Y(i) (HDR_H + 6 + (i) * (CARD_H + CARD_GAP))  // i = 0..3
// // card 0 FREQUENCY, 1 INPUT VOLTAGE, 2 GAIN MODE, 3 TEMPERATURE
// // last card ends at 34 + 4*70 - 6 = 308 < 320  ✔

// #define RCOL_X 246  // right column
// #define RCOL_W 226
// #define SCOPE_CY (HDR_H + 6)  // scope card
// #define SCOPE_CH 140
// #define WAVE_CY (SCOPE_CY + SCOPE_CH + CARD_GAP)  // waveform card
// #define WAVE_CH (320 - WAVE_CY - 6)               // fills to bottom

// // scope plotting area (inside scope card, below its label)
// #define SCOPE_X (RCOL_X + 6)
// #define SCOPE_Y (SCOPE_CY + 24)
// #define SCOPE_W (RCOL_W - 12)
// #define SCOPE_H (SCOPE_CH - 30)

// #define BORDER_COL 0x39E7  // dark grey border
// #define LABEL_COL TFT_LIGHTGREY

// // ---------------- change-detection state ----------------
// WaveType lastWave = (WaveType)255;
// float lastFreq = -999, lastVpp = -999, lastMean = -999, lastTemp = -999;
// int lastGain = -1;
// bool lastTempNA = false, lastVinNA = false;

// // ================= UI helpers =================

// // Draw one card frame + its label (called once in setup)
// void drawCard(int x, int y, int w, int h, const char* label) {
//   tft.drawRoundRect(x, y, w, h, CARD_R, BORDER_COL);
//   tft.setTextDatum(TL_DATUM);
//   tft.setTextColor(LABEL_COL, TFT_BLACK);
//   tft.drawString(label, x + 10, y + 7, 2);  // Font 2, 16 px
// }

// // Replace the value area of left-column card i with string s (Font 4, 26 px)
// void cardValue(int i, const char* s, uint16_t color) {
//   int x = CARD_X, y = CARD_Y(i);
//   tft.fillRect(x + 4, y + 27, CARD_W - 8, CARD_H - 31, TFT_BLACK);  // clear inside border only
//   tft.setTextDatum(TL_DATUM);
//   tft.setTextColor(color, TFT_BLACK);
//   tft.drawString(s, x + 12, y + 30, 4);
// }

// // Temperature value with a real ° symbol (drawn circle — fonts lack °)
// void cardTemp(float t) {
//   int x = CARD_X, y = CARD_Y(3);
//   tft.fillRect(x + 4, y + 27, CARD_W - 8, CARD_H - 31, TFT_BLACK);
//   tft.setTextDatum(TL_DATUM);
//   tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
//   if (isnan(t)) {
//     tft.drawString("N/A", x + 12, y + 30, 4);
//     return;
//   }
//   char s[16];
//   snprintf(s, sizeof(s), "%.1f", t);
//   int w = tft.drawString(s, x + 12, y + 30, 4);
//   int cx = x + 12 + w + 6;
//   tft.drawCircle(cx, y + 33, 3, TFT_SKYBLUE);  // ° symbol
//   tft.drawCircle(cx, y + 33, 2, TFT_SKYBLUE);
//   tft.drawString("C", cx + 7, y + 30, 4);
// }

// // Big centered waveform name in the right-hand waveform card
// void waveValue(WaveType w) {
//   int x = RCOL_X, y = WAVE_CY;
//   // Clear the full card area (old x2 text overflowed the borders), then
//   // redraw the frame + label to repair any damage.
//   tft.fillRect(x, y, RCOL_W, WAVE_CH, TFT_BLACK);
//   drawCard(x, y, RCOL_W, WAVE_CH, "WAVEFORM");

//   if (w == W_NOSIG) return;  // no signal -> empty card

//   tft.setTextDatum(MC_DATUM);
//   tft.setTextColor(waveColors[w], TFT_BLACK);
//   // Use x2 only if the name fits inside the card, otherwise x1
//   tft.setTextSize(2);
//   if (tft.textWidth(waveNames[w], 4) > RCOL_W - 16) tft.setTextSize(1);
//   tft.drawString(waveNames[w], x + RCOL_W / 2, y + 27 + (WAVE_CH - 31) / 2, 4);
//   tft.setTextSize(1);
//   tft.setTextDatum(TL_DATUM);
// }

// // ================= Setup =================
// void setup() {
//   Serial.begin(115200);
//   analogReadResolution(12);
//   pinMode(SIG_PIN, INPUT_ANALOG);
//   pinMode(THERM_PIN, INPUT_ANALOG);
//   for (int i = 0; i < 4; i++)
//     pinMode(GAIN_PINS[i], GAIN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

//   tft.init();
//   tft.setRotation(1);  // 480 x 320
//   tft.fillScreen(TFT_BLACK);

//   // Header
//   tft.fillRect(0, 0, 480, HDR_H, TFT_NAVY);
//   tft.setTextColor(TFT_WHITE, TFT_NAVY);
//   tft.setTextDatum(MC_DATUM);
//   tft.drawString("SIGNAL ANALYZER  rev3.1  2026-08-31", 240, HDR_H / 2, 2);
//   tft.setTextDatum(TL_DATUM);

//   // Cards (frames + labels drawn once)
//   drawCard(CARD_X, CARD_Y(0), CARD_W, CARD_H, "FREQUENCY");
//   drawCard(CARD_X, CARD_Y(1), CARD_W, CARD_H, "INPUT VOLTAGE");
//   drawCard(CARD_X, CARD_Y(2), CARD_W, CARD_H, "GAIN MODE");
//   drawCard(CARD_X, CARD_Y(3), CARD_W, CARD_H, "TEMPERATURE");
//   drawCard(RCOL_X, SCOPE_CY, RCOL_W, SCOPE_CH, "SCOPE");
//   drawCard(RCOL_X, WAVE_CY, RCOL_W, WAVE_CH, "WAVEFORM");

//   // Initial fallback values
//   cardValue(0, "N/A", TFT_WHITE);
//   cardValue(1, "N/A", TFT_MAGENTA);
//   cardValue(2, "N/A", TFT_GOLD);
//   cardTemp(NAN);
//   waveValue(W_NOSIG);

//   // // Initial fallback values
//   // cardValue(0, "1000.0 Hz", TFT_WHITE);
//   // cardValue(1, "1.00 Vpp", TFT_MAGENTA);
//   // cardValue(2, "MODE 2", TFT_GOLD);
//   // cardTemp(25.0f);

//   // delay(5000);
// }

// // ---------------- Acquisition (unchanged) ----------------
// float sampleSignal() {  // returns sample rate (SPS)
//   uint32_t t0 = micros();
//   for (int i = 0; i < NSAMP; i++) buf[i] = analogRead(SIG_PIN);
//   return (float)NSAMP * 1e6f / (float)(micros() - t0);
// }

// Features computeFeatures() {
//   Features f;
//   uint32_t sum = 0;
//   f.vmin = 4095;
//   f.vmax = 0;
//   for (int i = 0; i < NSAMP; i++) {
//     sum += buf[i];
//     if (buf[i] < f.vmin) f.vmin = buf[i];
//     if (buf[i] > f.vmax) f.vmax = buf[i];
//   }
//   f.mean = (float)sum / NSAMP;
//   f.vpp = f.vmax - f.vmin;
//   float sum2 = 0, sum4 = 0;
//   int above = 0;
//   for (int i = 0; i < NSAMP; i++) {
//     float d = buf[i] - f.mean;  // float BEFORE ^4 — int d^4 would overflow
//     float d2 = d * d;
//     sum2 += d2;
//     sum4 += d2 * d2;
//     if (buf[i] > f.mean) above++;
//   }
//   float m2 = sum2 / NSAMP;
//   float m4 = sum4 / NSAMP;
//   f.rms = sqrtf(m2);
//   f.kurt = (m2 > 1.0f) ? m4 / (m2 * m2) : 0;  // == avg of ((x-mean)/rms)^4
//   float peak = max(f.vmax - f.mean, f.mean - f.vmin);
//   f.crest = (f.rms > 1.0f) ? peak / f.rms : 0;  // kept for debug only
//   f.sym = (float)above / NSAMP;
//   return f;
// }

// // Kurtosis-based classifier. RAMP no longer returned (enum kept for compat).
// WaveType classify(const Features& f) {
//   if (f.vpp < MIN_VPP_COUNTS) return W_NOSIG;
//   if (f.kurt < KURT_SQUARE_MAX) return W_SQUARE;
//   if (f.kurt < KURT_SINE_MAX) return W_SINE;
//   if (f.kurt < KURT_TRI_MAX) return W_TRIANGLE;
//   return W_OTHER;
// }

// // Temporal stabilizer: a new class must repeat WAVE_CONFIRM_COUNT buffers
// // before the display changes. NO SIGNAL is accepted after 2 (input removed
// // should show promptly, but one dropout buffer still can't blank the card).
// WaveType stabilizeWave(WaveType newWave) {
//   static WaveType candidate = W_NOSIG;
//   static WaveType stable = W_NOSIG;
//   static uint8_t count = 0;
//   if (newWave == candidate) {
//     if (count < 255) count++;
//   } else {
//     candidate = newWave;
//     count = 1;
//   }
//   uint8_t need = (candidate == W_NOSIG) ? 2 : WAVE_CONFIRM_COUNT;
//   if (count >= need) stable = candidate;
//   return stable;
// }

// float measureFreq(const Features& f, float fs) {
//   if (f.vpp < MIN_VPP_COUNTS) return 0;
//   float hi = f.mean + f.vpp * 0.1f, lo = f.mean - f.vpp * 0.1f;
//   bool state = buf[0] > f.mean;
//   int firstX = -1, lastX = -1, nX = 0;
//   for (int i = 1; i < NSAMP; i++) {
//     if (!state && buf[i] > hi) {
//       state = true;
//       if (firstX < 0) firstX = i;
//       else lastX = i;
//       nX++;
//     } else if (state && buf[i] < lo) state = false;
//   }
//   if (nX < 2 || lastX <= firstX) return 0;
//   return (nX - 1) * fs / (float)(lastX - firstX);
// }

// int readGainMode() {  // returns 1..4, or 0 = none active
//   for (int i = 3; i >= 0; i--) {
//     int v = digitalRead(GAIN_PINS[i]);
//     if ((GAIN_ACTIVE_HIGH && v == HIGH) || (!GAIN_ACTIVE_HIGH && v == LOW)) return i + 1;
//   }
//   return 0;
// }

// float readTemperature() {
//   uint32_t acc = 0;
//   for (int i = 0; i < 16; i++) acc += analogRead(THERM_PIN);
//   float adc = acc / 16.0f;
//   if (adc < 5 || adc > 4090) return NAN;  // open / short
//   float r = THERM_RSERIES * adc / (4095.0f - adc);
//   float tK = 1.0f / (logf(r / THERM_R25) / THERM_BETA + 1.0f / T0_KELVIN);
//   return tK - 273.15f;
// }

// // ================= Display update =================
// void drawScope() {
//   tft.fillRect(SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H, TFT_BLACK);
//   // faint mid line for reference
//   tft.drawFastHLine(SCOPE_X, SCOPE_Y + SCOPE_H / 2, SCOPE_W, 0x2104);
//   int step = NSAMP / SCOPE_W, py = 0;
//   for (int x = 0; x < SCOPE_W; x++) {
//     int y = SCOPE_Y + SCOPE_H - 1 - (buf[x * step] * (SCOPE_H - 1)) / 4095;
//     if (x > 0) tft.drawLine(SCOPE_X + x - 1, py, SCOPE_X + x, y, TFT_GREENYELLOW);
//     py = y;
//   }
// }

// void updateDisplay(WaveType w, float freq, const Features& f, int gain, float temp) {
//   char s[24];

//   // WAVEFORM (right card)
//   if (w != lastWave) {
//     waveValue(w);
//     lastWave = w;
//   }

//   // FREQUENCY
//   if (fabsf(freq - lastFreq) > 0.5f) {
//     if (freq >= 1000) snprintf(s, sizeof(s), "%.2f kHz", freq / 1000);
//     else if (freq > 0) snprintf(s, sizeof(s), "%.1f Hz", freq);
//     else snprintf(s, sizeof(s), "N/A");
//     cardValue(0, s, TFT_WHITE);
//     lastFreq = freq;
//   }

//   // INPUT VOLTAGE — AC signal: show Vpp (+avg small); DC/no signal: show DC level
//   bool dcOnly = (f.vpp < MIN_VPP_COUNTS);
//   float vpp = f.vpp * VREF / 4095.0f * INPUT_ATTEN;
//   float vavg = f.mean * VREF / 4095.0f * INPUT_ATTEN;
//   if (dcOnly != lastVinNA || fabsf(vpp - lastVpp) > 0.02f || fabsf(vavg - lastMean) > 0.02f) {
//     if (dcOnly) snprintf(s, sizeof(s), "%.2f V", vavg);  // DC level
//     else snprintf(s, sizeof(s), "%.2f Vpp", vpp);
//     cardValue(1, s, TFT_MAGENTA);
//     tft.fillRect(CARD_X + 130, CARD_Y(1) + 7, CARD_W - 136, 18, TFT_BLACK);
//     tft.setTextColor(LABEL_COL, TFT_BLACK);
//     if (dcOnly) tft.drawString("DC", CARD_X + 140, CARD_Y(1) + 9, 2);
//     else {
//       char s2[16];
//       snprintf(s2, sizeof(s2), "avg %.2f V", vavg);
//       tft.drawString(s2, CARD_X + 140, CARD_Y(1) + 9, 2);
//     }
//     lastVpp = vpp;
//     lastMean = vavg;
//     lastVinNA = dcOnly;
//   }

//   // GAIN MODE
//   if (gain != lastGain) {
//     if (gain > 0) snprintf(s, sizeof(s), "MODE %d", gain);
//     else snprintf(s, sizeof(s), "N/A");
//     cardValue(2, s, TFT_GOLD);
//     lastGain = gain;
//   }

//   // TEMPERATURE
//   bool tNA = isnan(temp);
//   if (tNA != lastTempNA || (!tNA && fabsf(temp - lastTemp) > 0.05f)) {
//     cardTemp(temp);
//     lastTemp = temp;
//     lastTempNA = tNA;
//   }
// }

// // ================= Main loop =================
// uint32_t lastTempMs = 0;
// float temperature = NAN;

// void loop() {
//   float fs = sampleSignal();
//   Features f = computeFeatures();
//   WaveType rawWave = classify(f);
//   WaveType w = stabilizeWave(rawWave);
//   float freq = measureFreq(f, fs);
//   int gain = readGainMode();

//   if (millis() - lastTempMs >= 500) {
//     temperature = readTemperature();
//     lastTempMs = millis();
//   }

//   drawScope();
//   updateDisplay(w, freq, f, gain, temperature);

//   Serial.print("fs=");
//   Serial.print(fs, 0);
//   Serial.print(" vpp=");
//   Serial.print(f.vpp, 0);
//   Serial.print(" mean=");
//   Serial.print(f.mean, 1);
//   Serial.print(" rms=");
//   Serial.print(f.rms, 1);
//   Serial.print(" crest=");
//   Serial.print(f.crest, 3);
//   Serial.print(" kurt=");
//   Serial.print(f.kurt, 3);
//   Serial.print(" sym=");
//   Serial.print(f.sym, 3);
//   Serial.print(" gain=");
//   Serial.print(gain);
//   Serial.print(" raw=");
//   Serial.print(waveNames[rawWave]);
//   Serial.print(" stable=");
//   Serial.print(waveNames[w]);
//   Serial.print(" f=");
//   Serial.print(freq, 1);
//   Serial.print("Hz T=");
//   Serial.println(temperature, 2);

//   delay(100);
// }



/*
 * SIGNAL ANALYZER PANEL — rev 3.2 (float display fix) — 2026-08-31
 * STM32F411 Black Pill + 3.5" ILI9488 480x320 TFT shield (8-bit parallel)
 *
 * PIN MAP rev 3 (unchanged, matches WIRING.md and User_Setup.h 2026-08-30):
 *   PA0        Signal input  (ADC1_IN0)
 *   PA1        Thermistor    (ADC1_IN1)
 *   PA2..PA5   GAIN mode inputs 1..4 (digital in, active HIGH by default)
 *   PA6        Fan PWM (reserved)
 *   PB0..PB7   LCD D0..D7
 *   PB9  CS | PB10 DC | PB12 RST | PB13 WR | PB14 RD
 *
 * rev 3.1 UI: separate rounded-corner cards per parameter, no overlap,
 * N/A fallbacks, units (Hz / V / °C), scope + waveform cards on the right.
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "analyzer_types.h"
TFT_eSPI tft = TFT_eSPI();

// ---------------- Pins (rev 3) ----------------
#define SIG_PIN PA0
#define THERM_PIN PA1
const int GAIN_PINS[4] = { PA2, PA3, PA4, PA5 };
#define GAIN_ACTIVE_HIGH 1  // set 0 if your gain lines are active LOW

// ---------------- Input scaling ----------------
#define VREF 3.3f
#define INPUT_ATTEN 1.0f  // divider ratio for TRUE input volts
                          // direct = 1.0f | 10k/10k = 2.0f | 68k/10k = 7.8f
                          // classification is shape-based; ATTEN does not affect it

// ---------------- Thermistor — CONFIRM THESE ----------------
#define THERM_R25 10000.0f
#define THERM_BETA 3950.0f
#define THERM_RSERIES 10000.0f
#define T0_KELVIN 298.15f

// ---------------- Sampling / classification ----------------
#define NSAMP 512
static uint16_t buf[NSAMP];
#define MIN_VPP_COUNTS 80

// Kurtosis (m4/m2^2) is the primary shape feature — averaged over all 512
// samples, so single noisy ADC samples can't flip the class (unlike crest).
// Theoretical: square 1.0 | sine 1.5 | triangle 1.8. Tune from Serial "kurt=".
#define KURT_SQUARE_MAX 1.30f
#define KURT_SINE_MAX 1.68f
#define KURT_TRI_MAX 2.10f
#define WAVE_CONFIRM_COUNT 3  // consecutive buffers to accept a new class
// (crest-factor thresholds removed — crest kept in Features for debug only)

const char* waveNames[] = { "NO SIGNAL", "SINE", "SQUARE", "TRIANGLE", "RAMP", "OTHER" };
const uint16_t waveColors[] = { TFT_DARKGREY, TFT_CYAN, TFT_YELLOW, TFT_GREEN, TFT_ORANGE, TFT_RED };

// ================= UI LAYOUT (480x320 landscape) =================
// Header bar across the top; 4 stat cards in the left column;
// scope card + waveform card in the right column. No region overlaps.
#define HDR_H 28

#define CARD_X 8  // left column
#define CARD_W 230
#define CARD_H 64
#define CARD_GAP 6
#define CARD_R 8                                           // corner radius
#define CARD_Y(i) (HDR_H + 6 + (i) * (CARD_H + CARD_GAP))  // i = 0..3
// card 0 FREQUENCY, 1 INPUT VOLTAGE, 2 GAIN MODE, 3 TEMPERATURE
// last card ends at 34 + 4*70 - 6 = 308 < 320  ✔

#define RCOL_X 246  // right column
#define RCOL_W 226
#define SCOPE_CY (HDR_H + 6)  // scope card
#define SCOPE_CH 140
#define WAVE_CY (SCOPE_CY + SCOPE_CH + CARD_GAP)  // waveform card
#define WAVE_CH (320 - WAVE_CY - 6)               // fills to bottom

// scope plotting area (inside scope card, below its label)
#define SCOPE_X (RCOL_X + 6)
#define SCOPE_Y (SCOPE_CY + 24)
#define SCOPE_W (RCOL_W - 12)
#define SCOPE_H (SCOPE_CH - 30)

#define BORDER_COL 0x39E7  // dark grey border
#define LABEL_COL TFT_LIGHTGREY

// ---------------- change-detection state ----------------
WaveType lastWave = (WaveType)255;
float lastFreq = -999, lastVpp = -999, lastMean = -999, lastTemp = -999;
int lastGain = -1;
bool lastTempNA = false, lastVinNA = false;

// ================= UI helpers =================

// Draw one card frame + its label (called once in setup)
void drawCard(int x, int y, int w, int h, const char* label) {
  tft.drawRoundRect(x, y, w, h, CARD_R, BORDER_COL);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(LABEL_COL, TFT_BLACK);
  tft.drawString(label, x + 10, y + 7, 2);  // Font 2, 16 px
}

// Replace the value area of left-column card i with string s (Font 4, 26 px)
void cardValue(int i, const char* s, uint16_t color) {
  int x = CARD_X, y = CARD_Y(i);
  tft.fillRect(x + 4, y + 27, CARD_W - 8, CARD_H - 31, TFT_BLACK);  // clear inside border only
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(s, x + 12, y + 30, 4);
}

// Format a float without relying on printf/snprintf %f support.
// Some embedded builds omit %f from printf-family functions to save flash.
// dtostrf() is provided by the Arduino core and reliably converts floats.
void formatFloatText(char* out, size_t outSize, float value, uint8_t decimals,
                     const char* prefix = "", const char* suffix = "") {
  char num[24];
  dtostrf(value, 1, decimals, num);
  snprintf(out, outSize, "%s%s%s", prefix, num, suffix);
}

// Temperature value with a real ° symbol (drawn circle — fonts lack °)
void cardTemp(float t) {
  int x = CARD_X, y = CARD_Y(3);
  tft.fillRect(x + 4, y + 27, CARD_W - 8, CARD_H - 31, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  if (isnan(t)) {
    tft.drawString("N/A", x + 12, y + 30, 4);
    return;
  }
  char s[16];
  dtostrf(t, 1, 1, s);
  int w = tft.drawString(s, x + 12, y + 30, 4);
  int cx = x + 12 + w + 6;
  tft.drawCircle(cx, y + 33, 3, TFT_SKYBLUE);  // ° symbol
  tft.drawCircle(cx, y + 33, 2, TFT_SKYBLUE);
  tft.drawString("C", cx + 7, y + 30, 4);
}

// Big centered waveform name in the right-hand waveform card
void waveValue(WaveType w) {
  int x = RCOL_X, y = WAVE_CY;
  // Clear the full card area (old x2 text overflowed the borders), then
  // redraw the frame + label to repair any damage.
  tft.fillRect(x, y, RCOL_W, WAVE_CH, TFT_BLACK);
  drawCard(x, y, RCOL_W, WAVE_CH, "WAVEFORM");

  if (w == W_NOSIG) return;  // no signal -> empty card

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(waveColors[w], TFT_BLACK);
  // Use x2 only if the name fits inside the card, otherwise x1
  tft.setTextSize(2);
  if (tft.textWidth(waveNames[w], 4) > RCOL_W - 16) tft.setTextSize(1);
  tft.drawString(waveNames[w], x + RCOL_W / 2, y + 27 + (WAVE_CH - 31) / 2, 4);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(SIG_PIN, INPUT_ANALOG);
  pinMode(THERM_PIN, INPUT_ANALOG);
  for (int i = 0; i < 4; i++)
    pinMode(GAIN_PINS[i], GAIN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);  // 480 x 320
  tft.fillScreen(TFT_BLACK);

  // Header
  tft.fillRect(0, 0, 480, HDR_H, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SIGNAL ANALYZER  rev3.2  2026-08-31", 240, HDR_H / 2, 2);
  tft.setTextDatum(TL_DATUM);

  // Cards (frames + labels drawn once)
  drawCard(CARD_X, CARD_Y(0), CARD_W, CARD_H, "FREQUENCY");
  drawCard(CARD_X, CARD_Y(1), CARD_W, CARD_H, "INPUT VOLTAGE");
  drawCard(CARD_X, CARD_Y(2), CARD_W, CARD_H, "GAIN MODE");
  drawCard(CARD_X, CARD_Y(3), CARD_W, CARD_H, "TEMPERATURE");
  drawCard(RCOL_X, SCOPE_CY, RCOL_W, SCOPE_CH, "SCOPE");
  drawCard(RCOL_X, WAVE_CY, RCOL_W, WAVE_CH, "WAVEFORM");

  // Initial fallback values
  cardValue(0, "N/A", TFT_WHITE);
  cardValue(1, "N/A", TFT_MAGENTA);
  cardValue(2, "N/A", TFT_GOLD);
  cardTemp(NAN);
  waveValue(W_NOSIG);

  // // Initial fallback values
  // cardValue(0, "1000.0 Hz", TFT_WHITE);
  // cardValue(1, "1.00 Vpp", TFT_MAGENTA);
  // cardValue(2, "MODE 2", TFT_GOLD);
  // cardTemp(25.0f);

  // delay(5000);
}

// ---------------- Acquisition (unchanged) ----------------
float sampleSignal() {  // returns sample rate (SPS)
  uint32_t t0 = micros();
  for (int i = 0; i < NSAMP; i++) buf[i] = analogRead(SIG_PIN);
  return (float)NSAMP * 1e6f / (float)(micros() - t0);
}

Features computeFeatures() {
  Features f;
  uint32_t sum = 0;
  f.vmin = 4095;
  f.vmax = 0;
  for (int i = 0; i < NSAMP; i++) {
    sum += buf[i];
    if (buf[i] < f.vmin) f.vmin = buf[i];
    if (buf[i] > f.vmax) f.vmax = buf[i];
  }
  f.mean = (float)sum / NSAMP;
  f.vpp = f.vmax - f.vmin;
  float sum2 = 0, sum4 = 0;
  int above = 0;
  for (int i = 0; i < NSAMP; i++) {
    float d = buf[i] - f.mean;  // float BEFORE ^4 — int d^4 would overflow
    float d2 = d * d;
    sum2 += d2;
    sum4 += d2 * d2;
    if (buf[i] > f.mean) above++;
  }
  float m2 = sum2 / NSAMP;
  float m4 = sum4 / NSAMP;
  f.rms = sqrtf(m2);
  f.kurt = (m2 > 1.0f) ? m4 / (m2 * m2) : 0;  // == avg of ((x-mean)/rms)^4
  float peak = max(f.vmax - f.mean, f.mean - f.vmin);
  f.crest = (f.rms > 1.0f) ? peak / f.rms : 0;  // kept for debug only
  f.sym = (float)above / NSAMP;
  return f;
}

// Kurtosis-based classifier. RAMP no longer returned (enum kept for compat).
WaveType classify(const Features& f) {
  if (f.vpp < MIN_VPP_COUNTS) return W_NOSIG;
  if (f.kurt < KURT_SQUARE_MAX) return W_SQUARE;
  if (f.kurt < KURT_SINE_MAX) return W_SINE;
  if (f.kurt < KURT_TRI_MAX) return W_TRIANGLE;
  return W_OTHER;
}

// Temporal stabilizer: a new class must repeat WAVE_CONFIRM_COUNT buffers
// before the display changes. NO SIGNAL is accepted after 2 (input removed
// should show promptly, but one dropout buffer still can't blank the card).
WaveType stabilizeWave(WaveType newWave) {
  static WaveType candidate = W_NOSIG;
  static WaveType stable = W_NOSIG;
  static uint8_t count = 0;
  if (newWave == candidate) {
    if (count < 255) count++;
  } else {
    candidate = newWave;
    count = 1;
  }
  uint8_t need = (candidate == W_NOSIG) ? 2 : WAVE_CONFIRM_COUNT;
  if (count >= need) stable = candidate;
  return stable;
}

float measureFreq(const Features& f, float fs) {
  if (f.vpp < MIN_VPP_COUNTS) return 0;
  float hi = f.mean + f.vpp * 0.1f, lo = f.mean - f.vpp * 0.1f;
  bool state = buf[0] > f.mean;
  int firstX = -1, lastX = -1, nX = 0;
  for (int i = 1; i < NSAMP; i++) {
    if (!state && buf[i] > hi) {
      state = true;
      if (firstX < 0) firstX = i;
      else lastX = i;
      nX++;
    } else if (state && buf[i] < lo) state = false;
  }
  if (nX < 2 || lastX <= firstX) return 0;
  return (nX - 1) * fs / (float)(lastX - firstX);
}

int readGainMode() {  // returns 1..4, or 0 = none active
  for (int i = 3; i >= 0; i--) {
    int v = digitalRead(GAIN_PINS[i]);
    if ((GAIN_ACTIVE_HIGH && v == HIGH) || (!GAIN_ACTIVE_HIGH && v == LOW)) return i + 1;
  }
  return 0;
}

float readTemperature() {
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) acc += analogRead(THERM_PIN);
  float adc = acc / 16.0f;
  if (adc < 5 || adc > 4090) return NAN;  // open / short
  float r = THERM_RSERIES * adc / (4095.0f - adc);
  float tK = 1.0f / (logf(r / THERM_R25) / THERM_BETA + 1.0f / T0_KELVIN);
  return tK - 273.15f;
}

// ================= Display update =================
void drawScope() {
  tft.fillRect(SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H, TFT_BLACK);
  // faint mid line for reference
  tft.drawFastHLine(SCOPE_X, SCOPE_Y + SCOPE_H / 2, SCOPE_W, 0x2104);
  int step = NSAMP / SCOPE_W, py = 0;
  for (int x = 0; x < SCOPE_W; x++) {
    int y = SCOPE_Y + SCOPE_H - 1 - (buf[x * step] * (SCOPE_H - 1)) / 4095;
    if (x > 0) tft.drawLine(SCOPE_X + x - 1, py, SCOPE_X + x, y, TFT_GREENYELLOW);
    py = y;
  }
}

void updateDisplay(WaveType w, float freq, const Features& f, int gain, float temp) {
  char s[24];

  // WAVEFORM (right card)
  if (w != lastWave) {
    waveValue(w);
    lastWave = w;
  }

  // FREQUENCY
  if (fabsf(freq - lastFreq) > 0.5f) {
    if (freq >= 1000.0f)
      formatFloatText(s, sizeof(s), freq / 1000.0f, 2, "", " kHz");
    else if (freq > 0.0f)
      formatFloatText(s, sizeof(s), freq, 1, "", " Hz");
    else
      strcpy(s, "N/A");

    cardValue(0, s, TFT_WHITE);
    lastFreq = freq;
  }

  // INPUT VOLTAGE — AC signal: show Vpp (+avg small); DC/no signal: show DC level
  bool dcOnly = (f.vpp < MIN_VPP_COUNTS);
  float vpp = f.vpp * VREF / 4095.0f * INPUT_ATTEN;
  float vavg = f.mean * VREF / 4095.0f * INPUT_ATTEN;

  if (dcOnly != lastVinNA ||
      fabsf(vpp - lastVpp) > 0.02f ||
      fabsf(vavg - lastMean) > 0.02f) {

    if (dcOnly)
      formatFloatText(s, sizeof(s), vavg, 2, "", " V");
    else
      formatFloatText(s, sizeof(s), vpp, 2, "", " Vpp");

    cardValue(1, s, TFT_MAGENTA);

    tft.fillRect(CARD_X + 130, CARD_Y(1) + 7, CARD_W - 136, 18, TFT_BLACK);
    tft.setTextColor(LABEL_COL, TFT_BLACK);

    if (dcOnly) {
      tft.drawString("DC", CARD_X + 140, CARD_Y(1) + 9, 2);
    } else {
      char s2[24];
      formatFloatText(s2, sizeof(s2), vavg, 2, "avg ", " V");
      tft.drawString(s2, CARD_X + 140, CARD_Y(1) + 9, 2);
    }

    lastVpp = vpp;
    lastMean = vavg;
    lastVinNA = dcOnly;
  }

  // GAIN MODE
  if (gain != lastGain) {
    if (gain > 0) snprintf(s, sizeof(s), "MODE %d", gain);
    else snprintf(s, sizeof(s), "N/A");
    cardValue(2, s, TFT_GOLD);
    lastGain = gain;
  }

  // TEMPERATURE
  bool tNA = isnan(temp);
  if (tNA != lastTempNA || (!tNA && fabsf(temp - lastTemp) > 0.05f)) {
    cardTemp(temp);
    lastTemp = temp;
    lastTempNA = tNA;
  }
}

// ================= Main loop =================
uint32_t lastTempMs = 0;
float temperature = NAN;

void loop() {
  float fs = sampleSignal();
  Features f = computeFeatures();
  WaveType rawWave = classify(f);
  WaveType w = stabilizeWave(rawWave);
  float freq = measureFreq(f, fs);
  int gain = readGainMode();

  if (millis() - lastTempMs >= 500) {
    temperature = readTemperature();
    lastTempMs = millis();
  }

  drawScope();
  updateDisplay(w, freq, f, gain, temperature);

  Serial.print("fs=");
  Serial.print(fs, 0);
  Serial.print(" vpp=");
  Serial.print(f.vpp, 0);
  Serial.print(" mean=");
  Serial.print(f.mean, 1);
  Serial.print(" rms=");
  Serial.print(f.rms, 1);
  Serial.print(" crest=");
  Serial.print(f.crest, 3);
  Serial.print(" kurt=");
  Serial.print(f.kurt, 3);
  Serial.print(" sym=");
  Serial.print(f.sym, 3);
  Serial.print(" gain=");
  Serial.print(gain);
  Serial.print(" raw=");
  Serial.print(waveNames[rawWave]);
  Serial.print(" stable=");
  Serial.print(waveNames[w]);
  Serial.print(" f=");
  Serial.print(freq, 1);
  Serial.print("Hz T=");
  Serial.println(temperature, 2);

  delay(100);
}
