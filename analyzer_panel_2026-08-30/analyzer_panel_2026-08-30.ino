/*
 * SIGNAL ANALYZER PANEL — rev 3 — 2026-08-30
 * STM32F411 Black Pill + 3.5" ILI9488 480x320 TFT shield (8-bit parallel)
 *
 * PIN MAP rev 3 (matches WIRING.md and User_Setup.h dated 2026-08-30):
 *   PA0        Signal input  (ADC1_IN0)  — via divider/protection network
 *   PA1        Thermistor    (ADC1_IN1)  — 3.3V -> Rseries -> PA1 -> NTC -> GND
 *   PA2..PA5   GAIN mode inputs 1..4 (digital in, active HIGH by default)
 *   PA6        Fan PWM (reserved, not used here)
 *   PB0..PB7   LCD D0..D7
 *   PB9  CS | PB10 DC | PB12 RST | PB13 WR | PB14 RD
 *
 * Interface shows: waveform type, frequency, INPUT VOLTAGE (Vpp + mean),
 * GAIN MODE (from PA2-PA5), temperature, mini scope trace.
 */

#include <TFT_eSPI.h>
#include "analyzer_types.h"
TFT_eSPI tft = TFT_eSPI();

// ---------------- Pins (rev 3, 2026-08-30) ----------------
#define SIG_PIN     PA0
#define THERM_PIN   PA1
const int GAIN_PINS[4] = {PA2, PA3, PA4, PA5};
#define GAIN_ACTIVE_HIGH 1   // set 0 if your gain lines are active LOW

// ---------------- Input scaling ----------------
#define VREF        3.3f
#define INPUT_ATTEN 1.0f     // set to your divider ratio (e.g. 7.8f) to show TRUE input volts

// ---------------- Thermistor — CONFIRM THESE ----------------
#define THERM_R25      10000.0f
#define THERM_BETA     3950.0f
#define THERM_RSERIES  10000.0f
#define T0_KELVIN      298.15f

// ---------------- Sampling / classification ----------------
#define NSAMP 512
static uint16_t buf[NSAMP];
#define MIN_VPP_COUNTS 80
#define CF_SQUARE_MAX  1.20f
#define CF_SINE_MAX    1.55f
#define CF_TRI_MAX     1.90f
#define SYM_TOL        0.10f

const char* waveNames[]  = {"NO SIGNAL", "SINE", "SQUARE", "TRIANGLE", "RAMP", "OTHER"};
const uint16_t waveColors[] = {TFT_DARKGREY, TFT_CYAN, TFT_YELLOW, TFT_GREEN, TFT_ORANGE, TFT_RED};

// ---------------- Layout (480x320 landscape) ----------------
#define HDR_H    32
#define COL_X    12
#define VAL_X    12
#define ROW_WAVE 44
#define ROW_FREQ 104
#define ROW_VIN  164
#define ROW_GAIN 224
#define ROW_TEMP 274
#define SCOPE_X  262
#define SCOPE_Y  52
#define SCOPE_W  206
#define SCOPE_H  90

WaveType lastWave = (WaveType)255;
float lastFreq = -999, lastVpp = -999, lastMean = -999, lastTemp = -999;
int lastGain = -1;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(SIG_PIN, INPUT_ANALOG);
  pinMode(THERM_PIN, INPUT_ANALOG);
  for (int i = 0; i < 4; i++)
    pinMode(GAIN_PINS[i], GAIN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);            // 480 x 320
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 480, HDR_H, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SIGNAL ANALYZER  rev3  2026-08-30", 240, HDR_H / 2, 2);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("WAVEFORM",    COL_X, ROW_WAVE, 2);
  tft.drawString("FREQUENCY",   COL_X, ROW_FREQ, 2);
  tft.drawString("INPUT",       COL_X, ROW_VIN,  2);
  tft.drawString("GAIN MODE",   COL_X, ROW_GAIN, 2);
  tft.drawString("TEMPERATURE", COL_X, ROW_TEMP, 2);
  tft.drawRect(SCOPE_X - 1, SCOPE_Y - 1, SCOPE_W + 2, SCOPE_H + 2, TFT_DARKGREY);
}

// ---------------- Acquisition ----------------
float sampleSignal() {                       // returns sample rate (SPS)
  uint32_t t0 = micros();
  for (int i = 0; i < NSAMP; i++) buf[i] = analogRead(SIG_PIN);
  return (float)NSAMP * 1e6f / (float)(micros() - t0);
}

Features computeFeatures() {
  Features f; uint32_t sum = 0;
  f.vmin = 4095; f.vmax = 0;
  for (int i = 0; i < NSAMP; i++) {
    sum += buf[i];
    if (buf[i] < f.vmin) f.vmin = buf[i];
    if (buf[i] > f.vmax) f.vmax = buf[i];
  }
  f.mean = (float)sum / NSAMP;
  f.vpp  = f.vmax - f.vmin;
  float sumsq = 0; int above = 0;
  for (int i = 0; i < NSAMP; i++) {
    float d = buf[i] - f.mean;
    sumsq += d * d;
    if (buf[i] > f.mean) above++;
  }
  f.rms = sqrtf(sumsq / NSAMP);
  float peak = max(f.vmax - f.mean, f.mean - f.vmin);
  f.crest = (f.rms > 1.0f) ? peak / f.rms : 0;
  f.sym = (float)above / NSAMP;
  return f;
}

WaveType classify(const Features& f) {
  if (f.vpp < MIN_VPP_COUNTS) return W_NOSIG;
  if (f.crest < CF_SQUARE_MAX) return W_SQUARE;
  if (f.crest < CF_SINE_MAX)   return W_SINE;
  if (f.crest < CF_TRI_MAX)
    return (fabsf(f.sym - 0.5f) < SYM_TOL) ? W_TRIANGLE : W_RAMP;
  return W_OTHER;
}

float measureFreq(const Features& f, float fs) {
  if (f.vpp < MIN_VPP_COUNTS) return 0;
  float hi = f.mean + f.vpp * 0.1f, lo = f.mean - f.vpp * 0.1f;
  bool state = buf[0] > f.mean;
  int firstX = -1, lastX = -1, nX = 0;
  for (int i = 1; i < NSAMP; i++) {
    if (!state && buf[i] > hi) { state = true; if (firstX < 0) firstX = i; else lastX = i; nX++; }
    else if (state && buf[i] < lo) state = false;
  }
  if (nX < 2 || lastX <= firstX) return 0;
  return (nX - 1) * fs / (float)(lastX - firstX);
}

int readGainMode() {                        // returns 1..4, or 0 = none active
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
  if (adc < 5 || adc > 4090) return NAN;    // open / short
  float r  = THERM_RSERIES * adc / (4095.0f - adc);
  float tK = 1.0f / (logf(r / THERM_R25) / THERM_BETA + 1.0f / T0_KELVIN);
  return tK - 273.15f;
}

// ---------------- Display ----------------
void drawScope() {
  tft.fillRect(SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H, TFT_BLACK);
  int step = NSAMP / SCOPE_W, py = 0;
  for (int x = 0; x < SCOPE_W; x++) {
    int y = SCOPE_Y + SCOPE_H - 1 - (buf[x * step] * (SCOPE_H - 1)) / 4095;
    if (x > 0) tft.drawLine(SCOPE_X + x - 1, py, SCOPE_X + x, y, TFT_GREENYELLOW);
    py = y;
  }
}

void bigValue(const char* s, int y, uint16_t color) {
  tft.fillRect(0, y + 16, 250, 30, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);                       // Font 4 x2 (Font 6 lacks letters!)
  tft.drawString(s, VAL_X, y + 16, 4);
  tft.setTextSize(1);
}

void updateDisplay(WaveType w, float freq, const Features& f, int gain, float temp) {
  char s[32];

  if (w != lastWave) { bigValue(waveNames[w], ROW_WAVE, waveColors[w]); lastWave = w; }

  if (fabsf(freq - lastFreq) > 0.5f) {
    if (freq >= 1000)     snprintf(s, sizeof(s), "%.2f kHz ", freq / 1000);
    else if (freq > 0)    snprintf(s, sizeof(s), "%.1f Hz   ", freq);
    else                  snprintf(s, sizeof(s), "---       ");
    bigValue(s, ROW_FREQ, TFT_WHITE);
    lastFreq = freq;
  }

  float vpp  = f.vpp  * VREF / 4095.0f * INPUT_ATTEN;
  float vavg = f.mean * VREF / 4095.0f * INPUT_ATTEN;
  if (fabsf(vpp - lastVpp) > 0.02f || fabsf(vavg - lastMean) > 0.02f) {
    snprintf(s, sizeof(s), "%.2fVpp %.2fV ", vpp, vavg);
    tft.fillRect(0, ROW_VIN + 16, 250, 26, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString(s, VAL_X, ROW_VIN + 16, 4);   // 1x font 4 (fits Vpp + avg)
    lastVpp = vpp; lastMean = vavg;
  }

  if (gain != lastGain) {
    if (gain > 0) snprintf(s, sizeof(s), "MODE %d ", gain);
    else          snprintf(s, sizeof(s), "NONE   ");
    bigValue(s, ROW_GAIN, TFT_GOLD);
    lastGain = gain;
  }

  if (isnan(temp)) snprintf(s, sizeof(s), "SENSOR?  ");
  else             snprintf(s, sizeof(s), "%.1f C   ", temp);
  if (fabsf(temp - lastTemp) > 0.05f || isnan(temp)) {
    bigValue(s, ROW_TEMP, TFT_SKYBLUE);
    lastTemp = temp;
  }
}

// ---------------- Main loop ----------------
uint32_t lastTempMs = 0;
float temperature = NAN;

void loop() {
  float fs = sampleSignal();
  Features f = computeFeatures();
  WaveType w = classify(f);
  float freq = measureFreq(f, fs);
  int gain = readGainMode();

  if (millis() - lastTempMs >= 500) { temperature = readTemperature(); lastTempMs = millis(); }

  drawScope();
  updateDisplay(w, freq, f, gain, temperature);

  Serial.print("fs="); Serial.print(fs, 0);
  Serial.print(" vpp="); Serial.print(f.vpp, 0);
  Serial.print(" crest="); Serial.print(f.crest, 3);
  Serial.print(" sym="); Serial.print(f.sym, 3);
  Serial.print(" gain="); Serial.print(gain);
  Serial.print(" -> "); Serial.print(waveNames[w]);
  Serial.print(" f="); Serial.print(freq, 1);
  Serial.print("Hz T="); Serial.println(temperature, 2);

  delay(100);
}
