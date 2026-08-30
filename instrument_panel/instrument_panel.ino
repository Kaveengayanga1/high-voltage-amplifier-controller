/*
 * Signal Analyzer + Temperature Monitor — STM32F411 Black Pill + ILI9488 480x320
 * Pin map (FINAL — matches User_Setup.h):
 *   LCD data D0..D7 : PA0..PA7
 *   LCD RST/CS/DC   : PB12 / PB9 / PB10
 *   LCD WR/RD       : PB13 / PB14   (moved off PB0/PB1)
 *   Signal input    : PB0  (ADC1_IN8)  — via divider + protection
 *   Thermistor      : PB1  (ADC1_IN9)  — 3.3V -> Rseries -> PB1 -> NTC -> GND
 *
 * Classification: crest factor + symmetry, frequency via zero-crossing.
 * NOTE: uses analogRead() (~50 kSPS ceiling) — fine up to ~4-5 kHz signals.
 * DMA upgrade needed for full 11 kHz range (next step).
 */

#include <TFT_eSPI.h>
#include "analyzer_types.h"
TFT_eSPI tft = TFT_eSPI();

// ---------------- Pins ----------------
#define SIG_PIN    PA0   // ADC1_IN0 (KEY button also here: pressing it reads 0 - harmless)
#define THERM_PIN  PA1   // ADC1_IN1

// Future peripherals (rev 3 pin map):
//   PA2..PA5 = GAIN control 1..4 (digital out)
//   PA6      = Fan PWM (TIM3_CH1)

// ---------------- Thermistor — UPDATE WITH YOUR MEASURED VALUES ----------------
#define THERM_R25      10000.0f  // NTC resistance at 25 degC  <-- confirm
#define THERM_BETA     3950.0f   // Beta coefficient           <-- confirm
#define THERM_RSERIES  10000.0f  // series resistor value      <-- confirm
#define T0_KELVIN      298.15f   // 25 degC

// ---------------- Sampling ----------------
#define NSAMP 512
static uint16_t buf[NSAMP];

// ---------------- Classification thresholds ----------------
#define MIN_VPP_COUNTS   80      // below this -> NO SIGNAL (~65 mV)
#define CF_SQUARE_MAX    1.20f
#define CF_SINE_MAX      1.55f
#define CF_TRI_MAX       1.90f
#define SYM_TOL          0.10f   // |sym-0.5| < tol -> symmetric (triangle)

const char* waveNames[] = {"NO SIGNAL", "SINE", "SQUARE", "TRIANGLE", "RAMP", "OTHER"};
const uint16_t waveColors[] = {TFT_DARKGREY, TFT_CYAN, TFT_YELLOW, TFT_GREEN, TFT_ORANGE, TFT_RED};

// ---------------- Layout (480x320 landscape) ----------------
#define HDR_H     36
#define SIG_Y     (HDR_H + 8)
#define SCOPE_X   250
#define SCOPE_Y   60
#define SCOPE_W   200
#define SCOPE_H   80
#define TEMP_Y    210

WaveType lastWave = (WaveType)255;
float lastFreq = -1, lastTemp = -999;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(SIG_PIN, INPUT_ANALOG);
  pinMode(THERM_PIN, INPUT_ANALOG);

  tft.init();
  tft.setRotation(1);          // 480 wide x 320 tall
  tft.fillScreen(TFT_BLACK);

  // Header
  tft.fillRect(0, 0, 480, HDR_H, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SIGNAL ANALYZER + TEMP MONITOR", 240, HDR_H / 2, 4);

  // Static labels
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("WAVEFORM", 12, SIG_Y, 2);
  tft.drawString("FREQUENCY", 12, 140, 2);
  tft.drawString("TEMPERATURE", 12, TEMP_Y, 2);
  tft.drawRect(SCOPE_X - 1, SCOPE_Y - 1, SCOPE_W + 2, SCOPE_H + 2, TFT_DARKGREY);
}

// ---------------- Sampling ----------------
// Returns effective sample rate in samples/sec
float sampleSignal() {
  uint32_t t0 = micros();
  for (int i = 0; i < NSAMP; i++) buf[i] = analogRead(SIG_PIN);
  uint32_t dt = micros() - t0;
  return (float)NSAMP * 1e6f / (float)dt;
}

// ---------------- Feature extraction + classification ----------------
Features computeFeatures() {
  Features f;
  uint32_t sum = 0;
  f.vmin = 4095; f.vmax = 0;
  for (int i = 0; i < NSAMP; i++) {
    sum += buf[i];
    if (buf[i] < f.vmin) f.vmin = buf[i];
    if (buf[i] > f.vmax) f.vmax = buf[i];
  }
  f.mean = (float)sum / NSAMP;
  f.vpp = f.vmax - f.vmin;

  float sumsq = 0;
  int above = 0;
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
  if (f.crest < CF_TRI_MAX) {
    // Triangle vs ramp: triangle is symmetric about mean
    return (fabsf(f.sym - 0.5f) < SYM_TOL) ? W_TRIANGLE : W_RAMP;
  }
  return W_OTHER;
}

// Frequency via zero (mean) crossings with hysteresis
float measureFreq(const Features& f, float fs) {
  if (f.vpp < MIN_VPP_COUNTS) return 0;
  float hi = f.mean + f.vpp * 0.1f;
  float lo = f.mean - f.vpp * 0.1f;
  bool state = buf[0] > f.mean;
  int firstX = -1, lastX = -1, nX = 0;
  for (int i = 1; i < NSAMP; i++) {
    if (!state && buf[i] > hi) {          // rising crossing
      state = true;
      if (firstX < 0) firstX = i; else lastX = i;
      nX++;
    } else if (state && buf[i] < lo) {
      state = false;
    }
  }
  if (nX < 2 || lastX <= firstX) return 0;
  float periods = nX - 1;
  return periods * fs / (float)(lastX - firstX);
}

// ---------------- Temperature ----------------
float readTemperature() {
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) acc += analogRead(THERM_PIN);
  float adc = acc / 16.0f;
  if (adc < 5 || adc > 4090) return NAN;          // open/short
  // Wiring: 3.3V -> Rseries -> pin -> NTC -> GND  => V = 3.3 * Rntc/(Rs+Rntc)
  float r = THERM_RSERIES * adc / (4095.0f - adc);
  float tK = 1.0f / (logf(r / THERM_R25) / THERM_BETA + 1.0f / T0_KELVIN);
  return tK - 273.15f;
}

// ---------------- Display ----------------
void drawScope() {
  tft.fillRect(SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H, TFT_BLACK);
  int step = NSAMP / SCOPE_W;
  int py = 0;
  for (int x = 0; x < SCOPE_W; x++) {
    int y = SCOPE_Y + SCOPE_H - 1 - (buf[x * step] * (SCOPE_H - 1)) / 4095;
    if (x > 0) tft.drawLine(SCOPE_X + x - 1, py, SCOPE_X + x, y, TFT_GREENYELLOW);
    py = y;
  }
}

void updateDisplay(WaveType w, float freq, float temp) {
  char s[24];

  if (w != lastWave) {
    tft.fillRect(0, SIG_Y + 20, 240, 60, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(waveColors[w], TFT_BLACK);
    tft.setTextSize(2);                             // Font 6 has digits only -
    tft.drawString(waveNames[w], 12, SIG_Y + 24, 4); // use Font 4 (full charset) at 2x
    tft.setTextSize(1);
    lastWave = w;
  }

  if (fabsf(freq - lastFreq) > 0.5f) {
    if (freq > 0) {
      if (freq >= 1000) snprintf(s, sizeof(s), "%.2f kHz ", freq / 1000);
      else              snprintf(s, sizeof(s), "%.1f Hz   ", freq);
    } else snprintf(s, sizeof(s), "---      ");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(s, 12, 162, 4);
    lastFreq = freq;
  }

  if (isnan(temp)) snprintf(s, sizeof(s), "SENSOR? ");
  else             snprintf(s, sizeof(s), "%.1f C  ", temp);
  if (fabsf(temp - lastTemp) > 0.05f || isnan(temp)) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(s, 12, TEMP_Y + 24, 4);
    tft.setTextSize(1);
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

  if (millis() - lastTempMs >= 500) {
    temperature = readTemperature();
    lastTempMs = millis();
  }

  drawScope();
  updateDisplay(w, freq, temperature);

  Serial.print("fs="); Serial.print(fs, 0);
  Serial.print(" vpp="); Serial.print(f.vpp, 0);
  Serial.print(" crest="); Serial.print(f.crest, 3);
  Serial.print(" sym="); Serial.print(f.sym, 3);
  Serial.print(" -> "); Serial.print(waveNames[w]);
  Serial.print(" f="); Serial.print(freq, 1);
  Serial.print("Hz T="); Serial.println(temperature, 2);

  delay(100);
}
