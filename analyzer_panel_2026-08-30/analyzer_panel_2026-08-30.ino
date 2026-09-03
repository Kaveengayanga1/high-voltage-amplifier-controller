/*
 * SIGNAL ANALYZER PANEL — rev 3.6 (NTC + PA0 reconstruction + fan control) — 2026-09-04
 * STM32F411 Black Pill + 3.5" ILI9488 480x320 TFT shield (8-bit parallel)
 *
 * PIN MAP rev 3 (unchanged, matches WIRING.md and User_Setup.h 2026-08-30):
 *   PA0        Signal input  (ADC1_IN0)
 *   PA1        Thermistor    (ADC1_IN1)
 *   PA2..PA5   GAIN mode inputs 1..4 (digital in, active HIGH by default)
 *   PA6        Cooling fan control output (active HIGH)
 *   PB0..PB7   LCD D0..D7
 *   PB9  CS | PB10 DC | PB12 RST | PB13 WR | PB14 RD
 *
 * rev 3.5: PA0 front end = 75k from Vin, 22k from +3.3V, 24k to GND,
 * followed by a TLV9062 unity-gain buffer. Firmware reconstructs the
 * original bipolar input voltage (Vmin, Vmax, Vpp and average).
 *
 * rev 3.6: PA6 turns the cooling fan ON above 40 C and OFF at/below 38 C
 * (2 C hysteresis). If the thermistor reading is invalid, fan can be forced ON
 * as a fail-safe using FAN_FAILSAFE_ON.
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "analyzer_types.h"
TFT_eSPI tft = TFT_eSPI();

// ---------------- Pins (rev 3) ----------------
#define SIG_PIN PA0
#define THERM_PIN PA1
#define FAN_PIN PA6
const int GAIN_PINS[4] = { PA2, PA3, PA4, PA5 };
#define GAIN_ACTIVE_HIGH 1  // set 0 if your gain lines are active LOW

// ---------------- PA0 signal front-end calibration ----------------
// Hardware:
//   Vin  -> RIN (75k)  -> VSHIFT
//   3.3V -> RBIAS(22k) -> VSHIFT
//   GND  -> RGND (24k) -> VSHIFT
//   VSHIFT -> TLV9062 voltage follower -> PA0
//
// With the nominal values below:
//   VPA0 ~= 0.132730 * Vin + 1.493213 V
//   Vin  ~= 7.534091 * VPA0 - 11.25 V
//
// For best accuracy, replace these nominal values with DMM-measured values.
#define SIGNAL_ADC_VREF_V     3.300f   // STM32 ADC VREF+/VDDA
#define SIGNAL_BIAS_V         3.300f   // supply feeding the 22k bias resistor
#define SIGNAL_RIN_OHM        75000.0f
#define SIGNAL_RBIAS_OHM      22000.0f
#define SIGNAL_RGND_OHM       24000.0f

// Final calibration trims after comparing with a trusted oscilloscope/source.
// Start with 1.000 and 0.000.
#define SIGNAL_CAL_GAIN       1.000f
#define SIGNAL_CAL_OFFSET_V   0.000f

// Rail-clipping warning. 32 counts ~= 25.8 mV with a 3.3 V reference.
#define ADC_CLIP_MARGIN_COUNTS 32

// Below this PA0 peak-to-peak span, treat the input as essentially DC for UI.
#define DC_DISPLAY_VPP_COUNTS 8

// ---------------- Thermistor calibration ----------------
// Wiring: 3.3 V -> fixed resistor -> PA1 -> NTC -> GND
// IMPORTANT: set these to the values measured with a multimeter for best accuracy.
#define THERM_RSERIES_OHM 10000.0f   // actual fixed resistor value
#define THERM_SUPPLY_V    3.300f     // voltage feeding the divider (AMS1117 output)
#define THERM_ADC_VREF    3.300f     // STM32 VDDA / ADC full-scale reference

// ---------------- Cooling fan control ----------------
// PA6 drives the base/gate driver stage. It must NOT power the fan directly.
// Fan turns ON only when temperature is greater than 40 C.
// Once ON, it stays ON until temperature falls to 38 C or below.
#define FAN_ON_TEMP_C       40.0f
#define FAN_OFF_TEMP_C      38.0f
#define FAN_FAILSAFE_ON     1       // 1 = fan ON if thermistor reading is invalid

bool fanOn = false;

// Cleaned calibration curve generated from your measured NTC data.
// Obvious non-physical outliers were rejected and the curve was forced to
// decrease smoothly with temperature.  Resistance values are in kOhm.
struct ThermCalPoint {
  float tempC;
  float resistanceK;
};

static const ThermCalPoint THERM_TABLE[] = {
  {  0.0f, 33.604f },
  {  5.0f, 24.903f },
  { 10.0f, 18.985f },
  { 15.0f, 14.823f },
  { 20.0f, 11.810f },
  { 25.0f,  9.570f },
  { 30.0f,  7.866f },
  { 35.0f,  6.543f },
  { 40.0f,  5.496f },
  { 45.0f,  4.654f },
  { 50.0f,  3.967f },
  { 55.0f,  3.400f },
  { 60.0f,  2.926f },
  { 65.0f,  2.527f },
  { 70.0f,  2.188f },
  { 75.0f,  1.898f },
  { 80.0f,  1.649f },
  { 85.0f,  1.433f },
  { 90.0f,  1.247f },
  { 95.0f,  1.085f },
  {100.0f,  0.943f }
};

static const size_t THERM_TABLE_COUNT = sizeof(THERM_TABLE) / sizeof(THERM_TABLE[0]);

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
bool lastTempNA = false, lastVinClipped = false;

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

  // Cooling fan control. Start OFF; first temperature reading will update it.
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  fanOn = false;

  for (int i = 0; i < 4; i++)
    pinMode(GAIN_PINS[i], GAIN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);  // 480 x 320
  tft.fillScreen(TFT_BLACK);

  // Header
  tft.fillRect(0, 0, 480, HDR_H, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SIGNAL ANALYZER  rev3.6", 240, HDR_H / 2, 2);
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

// ---------------- PA0 voltage reconstruction ----------------
float signalReverseGain() {
  // From KCL at VSHIFT:
  // Vin = VPA0 * (1 + RIN/RBIAS + RIN/RGND) - VBIAS * RIN/RBIAS
  return 1.0f +
         SIGNAL_RIN_OHM / SIGNAL_RBIAS_OHM +
         SIGNAL_RIN_OHM / SIGNAL_RGND_OHM;
}

float signalReverseOffset() {
  return -SIGNAL_BIAS_V * SIGNAL_RIN_OHM / SIGNAL_RBIAS_OHM;
}

float adcCountsToPa0Volts(float counts) {
  return counts * SIGNAL_ADC_VREF_V / 4095.0f;
}

float pa0VoltsToInputVolts(float vPa0) {
  float vinIdeal = signalReverseGain() * vPa0 + signalReverseOffset();
  return vinIdeal * SIGNAL_CAL_GAIN + SIGNAL_CAL_OFFSET_V;
}

float adcCountsToInputVolts(float counts) {
  return pa0VoltsToInputVolts(adcCountsToPa0Volts(counts));
}

struct SignalVoltages {
  float pa0Min;
  float pa0Max;
  float pa0Avg;
  float pa0Vpp;
  float vinMin;
  float vinMax;
  float vinAvg;
  float vinVpp;
  bool clipped;
};

SignalVoltages computeSignalVoltages(const Features& f) {
  SignalVoltages v;
  v.pa0Min = adcCountsToPa0Volts(f.vmin);
  v.pa0Max = adcCountsToPa0Volts(f.vmax);
  v.pa0Avg = adcCountsToPa0Volts(f.mean);
  v.pa0Vpp = adcCountsToPa0Volts(f.vpp);

  v.vinMin = pa0VoltsToInputVolts(v.pa0Min);
  v.vinMax = pa0VoltsToInputVolts(v.pa0Max);
  v.vinAvg = pa0VoltsToInputVolts(v.pa0Avg);
  v.vinVpp = v.vinMax - v.vinMin;

  v.clipped = (f.vmin <= ADC_CLIP_MARGIN_COUNTS ||
               f.vmax >= (4095 - ADC_CLIP_MARGIN_COUNTS));
  return v;
}

// ---------------- Acquisition ----------------
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

float resistanceToTemperature(float resistanceK) {
  // Calibrated range is 0..100 C.  Return N/A outside it instead of
  // extrapolating a temperature that has not been calibrated.
  if (resistanceK > THERM_TABLE[0].resistanceK ||
      resistanceK < THERM_TABLE[THERM_TABLE_COUNT - 1].resistanceK) {
    return NAN;
  }

  for (size_t i = 0; i < THERM_TABLE_COUNT - 1; i++) {
    float r1 = THERM_TABLE[i].resistanceK;
    float r2 = THERM_TABLE[i + 1].resistanceK;

    // NTC resistance decreases as temperature rises.
    if (resistanceK <= r1 && resistanceK >= r2) {
      // Interpolate in ln(R), which follows the NTC curve much better than
      // straight resistance interpolation between 5 C calibration points.
      float lr  = logf(resistanceK);
      float lr1 = logf(r1);
      float lr2 = logf(r2);
      float f = (lr - lr1) / (lr2 - lr1);
      return THERM_TABLE[i].tempC +
             f * (THERM_TABLE[i + 1].tempC - THERM_TABLE[i].tempC);
    }
  }

  return NAN;
}

float readTemperature() {
  // One dummy conversion helps the ADC settle after PA0 signal sampling.
  (void)analogRead(THERM_PIN);
  delayMicroseconds(40);

  // Average several readings to reduce display jitter.
  uint32_t acc = 0;
  const int n = 32;
  for (int i = 0; i < n; i++) acc += analogRead(THERM_PIN);
  float adc = acc / (float)n;

  // Open/short protection.
  if (adc < 5.0f || adc > 4090.0f) return NAN;

  // Convert ADC count to the actual PA1 node voltage.
  float vNode = adc * THERM_ADC_VREF / 4095.0f;
  if (vNode <= 0.0f || vNode >= THERM_SUPPLY_V) return NAN;

  // Divider: Vs -> Rfixed -> PA1 -> NTC -> GND
  // Rntc = Rfixed * Vnode / (Vs - Vnode)
  float resistanceOhm = THERM_RSERIES_OHM * vNode / (THERM_SUPPLY_V - vNode);
  float resistanceK = resistanceOhm / 1000.0f;

  return resistanceToTemperature(resistanceK);
}

void updateFanControl(float tempC) {
  // Fail-safe behavior for open/short/out-of-calibration thermistor readings.
  if (isnan(tempC)) {
#if FAN_FAILSAFE_ON
    fanOn = true;
#else
    fanOn = false;
#endif
  } else {
    // Hysteresis prevents rapid ON/OFF chatter around 40 C.
    if (!fanOn && tempC > FAN_ON_TEMP_C) {
      fanOn = true;
    } else if (fanOn && tempC <= FAN_OFF_TEMP_C) {
      fanOn = false;
    }
  }

  digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
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

  // INPUT VOLTAGE — reconstruct the original source voltage from PA0.
  // Because the transfer function is linear, Vmin/Vmax/average can all be
  // reverse-mapped, and Vin Vpp = VinMax - VinMin.
  SignalVoltages sv = computeSignalVoltages(f);
  bool dcOnly = (f.vpp < DC_DISPLAY_VPP_COUNTS);
  float displayVpp = (f.vpp < 4.0f) ? 0.0f : sv.vinVpp;

  if (sv.clipped != lastVinClipped ||
      fabsf(displayVpp - lastVpp) > 0.02f ||
      fabsf(sv.vinAvg - lastMean) > 0.02f) {

    if (sv.clipped) {
      strcpy(s, "CLIPPED");
    } else if (dcOnly) {
      formatFloatText(s, sizeof(s), sv.vinAvg, 2, "", " V");
    } else {
      formatFloatText(s, sizeof(s), displayVpp, 2, "", " Vpp");
    }

    cardValue(1, s, sv.clipped ? TFT_RED : TFT_MAGENTA);

    tft.fillRect(CARD_X + 130, CARD_Y(1) + 7, CARD_W - 136, 18, TFT_BLACK);
    tft.setTextColor(sv.clipped ? TFT_RED : LABEL_COL, TFT_BLACK);

    if (sv.clipped) {
      tft.drawString("ADC CLIP", CARD_X + 140, CARD_Y(1) + 9, 2);
    } else if (dcOnly) {
      tft.drawString("DC INPUT", CARD_X + 140, CARD_Y(1) + 9, 2);
    } else {
      char s2[24];
      formatFloatText(s2, sizeof(s2), sv.vinAvg, 2, "avg ", " V");
      tft.drawString(s2, CARD_X + 140, CARD_Y(1) + 9, 2);
    }

    lastVpp = displayVpp;
    lastMean = sv.vinAvg;
    lastVinClipped = sv.clipped;
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
    updateFanControl(temperature);
    lastTempMs = millis();
  }

  drawScope();
  updateDisplay(w, freq, f, gain, temperature);

  SignalVoltages sv = computeSignalVoltages(f);

  Serial.print("fs=");
  Serial.print(fs, 0);

  Serial.print(" ADC[min=");
  Serial.print(f.vmin);
  Serial.print(" max=");
  Serial.print(f.vmax);
  Serial.print(" avg=");
  Serial.print(f.mean, 1);
  Serial.print("]");

  Serial.print(" PA0[min=");
  Serial.print(sv.pa0Min, 3);
  Serial.print("V max=");
  Serial.print(sv.pa0Max, 3);
  Serial.print("V avg=");
  Serial.print(sv.pa0Avg, 3);
  Serial.print("V vpp=");
  Serial.print(sv.pa0Vpp, 3);
  Serial.print("V]");

  Serial.print(" VIN[min=");
  Serial.print(sv.vinMin, 3);
  Serial.print("V max=");
  Serial.print(sv.vinMax, 3);
  Serial.print("V avg=");
  Serial.print(sv.vinAvg, 3);
  Serial.print("V vpp=");
  Serial.print(sv.vinVpp, 3);
  Serial.print("V]");

  Serial.print(" clip=");
  Serial.print(sv.clipped ? "YES" : "NO");
  Serial.print(" rmsCnt=");
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
  Serial.print(temperature, 2);
  Serial.print("C fan=");
  Serial.println(fanOn ? "ON" : "OFF");

  delay(100);
}