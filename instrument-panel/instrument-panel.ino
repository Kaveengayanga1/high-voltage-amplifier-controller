/*
 * ============================================================================
 *  instrument-panel.ino — Signal Analyzer + Temperature + Gain Display
 *  STM32F411CEU6 Black Pill · Arduino-core-for-STM32 v2.12.0 · TFT_eSPI
 * ============================================================================
 *
 *  PIN MAP
 *  ----------------------------------------------------------------------
 *  LCD (ILI9488, 8-bit parallel — set in TFT_eSPI User_Setup.h):
 *      D0–D7  = PB0–PB7      CS = PB9    RS/DC = PB10
 *      RST    = PB12         WR = PB13   RD    = PB14
 *  Signal input   : PA0 (ADC1_IN0)  ← divider network (see input-circuit/)
 *  Thermistor     : PA1 (ADC1_IN1)  ← 3.3V —10k— PA1 —NTC— GND, 100nF at pin
 *  Gain sense     : PA8, PA9, PA10, PA15 (5V-tolerant, INPUT_PULLDOWN)
 *                   ← tap each selector line through 10k series resistor
 *  ----------------------------------------------------------------------
 *
 *  DISPLAY LAYOUT (480x320 landscape)
 *      Header bar | left: TYPE, FREQUENCY, Vpp, DC | right: TEMP, GAIN
 *      Shows "NO INPUT" when no signal is present.
 * ============================================================================
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "analysis.h"

TFT_eSPI tft = TFT_eSPI();

/* ========================== USER CONFIGURATION ========================== */

/* --- Input divider constants (MEASURE-AND-EDIT if using ±20 V variant) ---
 * ±10 V set (R1=10k, R2=4.3k, R3=3k):  K = 0.1502, OFFSET = 1.652
 * ±20 V set (R1=20k, R2=3.6k, R3=3k):  K = 0.0756, OFFSET = 1.664        */
#define DIV_K       0.1502f   // node voltage per input volt
#define DIV_OFFSET  1.652f    // node voltage at Vin = 0

/* --- Gain selector: sense pin -> displayed gain (EDIT to your 4 values) --- */
const uint8_t  GAIN_PINS[4]  = { PA8, PA9, PA10, PA15 };
const uint16_t GAIN_VALUES[4] = { 5, 10, 20, 50 };

/* --- NTC lookup table: REPLACE with YOUR measured resistances (ohms). ---
 * Must be in ascending temperature / descending resistance order.
 * Values below are typical 10k B3950 — placeholders only!                 */
#define NTC_FIXED_R 10000.0f  // fixed series resistor (top, to 3.3 V)
const float NTC_TEMP[] = {  0,  5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
                           55, 60, 65, 70, 75, 80, 85, 90, 95, 100 };
const float NTC_RES[]  = { 32650, 25390, 19900, 15710, 12490, 10000, 8057,
                            6531,  5327,  4369,  3603,  2986,  2488, 2083,
                            1752,  1481,  1258,  1072,   918,   789,  680 };
#define NTC_POINTS 21

/* ============================ ADC + CAPTURE ============================= */

#define N_SAMPLES 2048
static const uint32_t RATES[] = { 200000, 20000, 2000 };  // fast -> slow
#define N_RATES 3
#define MIN_CROSSINGS 6
#define VREF 3.3f
#define ADC_MAX 4095.0f
#define NO_SIGNAL_VPP 0.08f   // volts at the pin

uint16_t adcBuf[N_SAMPLES];
volatile bool captureDone = false;

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim3;

/* One-time clock/GPIO/DMA setup shared by both ADC uses */
static void adcBaseInit() {
  __HAL_RCC_ADC1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Pin  = GPIO_PIN_0 | GPIO_PIN_1;          // PA0 signal, PA1 thermistor
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);

  hdma_adc1.Instance = DMA2_Stream0;         // ADC1 requests: DMA2 Stream0 Ch0
  hdma_adc1.Init.Channel = DMA_CHANNEL_0;
  hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode = DMA_NORMAL;
  hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  HAL_DMA_Init(&hdma_adc1);

  hadc1.Instance = ADC1;                     // link before first HAL_ADC_Init
  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/* Configure ADC1: timer-triggered DMA capture of PA0 */
static void adcConfigCapture() {
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;   // 25 MHz ADC clock
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  HAL_ADC_Init(&hadc1);

  ADC_ChannelConfTypeDef s = {0};
  s.Channel = ADC_CHANNEL_0;                               // PA0
  s.Rank = 1;
  s.SamplingTime = ADC_SAMPLETIME_84CYCLES;  // suits ~1.5k source impedance,
  HAL_ADC_ConfigChannel(&hadc1, &s);         // fits 5 us period at 200 kSPS
}

/* Configure ADC1: software-started single conversions (thermistor on PA1) */
static uint16_t adcReadThermistorRaw() {
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  HAL_ADC_Init(&hadc1);

  ADC_ChannelConfTypeDef s = {0};
  s.Channel = ADC_CHANNEL_1;                               // PA1
  s.Rank = 1;
  s.SamplingTime = ADC_SAMPLETIME_480CYCLES;               // slow + accurate
  HAL_ADC_ConfigChannel(&hadc1, &s);

  uint32_t acc = 0;                                        // average 16 reads
  for (int i = 0; i < 16; i++) {
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    acc += HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
  }
  return acc / 16;
}

/* TIM3 update event paces the ADC at fs (APB1 timer clock = 100 MHz) */
static void timerInit(uint32_t fs) {
  __HAL_RCC_TIM3_CLK_ENABLE();
  uint32_t psc = 0, arr = 100000000UL / fs;
  while (arr > 65536UL) { psc++; arr = 100000000UL / ((psc + 1) * fs); }

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = psc;
  htim3.Init.Period = arr - 1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim3);

  TIM_MasterConfigTypeDef m = {0};
  m.MasterOutputTrigger = TIM_TRGO_UPDATE;
  m.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim3, &m);
}

extern "C" void DMA2_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_adc1); }

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h) {
  if (h->Instance == ADC1) { HAL_TIM_Base_Stop(&htim3); captureDone = true; }
}

/* Blocking capture of N_SAMPLES from PA0 at fs. Returns true on success. */
static bool capture(uint32_t fs) {
  adcConfigCapture();                        // (re)claim ADC for DMA mode
  captureDone = false;
  timerInit(fs);
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adcBuf, N_SAMPLES) != HAL_OK)
    return false;
  HAL_TIM_Base_Start(&htim3);

  uint32_t timeoutMs = (N_SAMPLES * 1000UL) / fs + 200;
  uint32_t t0 = millis();
  while (!captureDone) {
    if (millis() - t0 > timeoutMs) {
      HAL_TIM_Base_Stop(&htim3);
      HAL_ADC_Stop_DMA(&hadc1);
      return false;
    }
  }
  HAL_ADC_Stop_DMA(&hadc1);
  return true;
}

/* ========================== SIGNAL ANALYSIS ============================= */

/* struct Analysis is defined in analysis.h (must be in a header so the
 * Arduino-generated function prototypes at the top of the file compile). */

/* Rising mean-crossings with hysteresis + interpolation.
 * periodsOK = crossing intervals uniform (rejects noise-faked crossings). */
static int countCrossings(float mean, float hyst, float &firstT, float &lastT,
                          bool &periodsOK) {
  int n = 0;
  bool armed = false;
  float prev = adcBuf[0], prevT = 0, minP = 1e9f, maxP = 0;
  firstT = lastT = 0;
  for (int i = 1; i < N_SAMPLES; i++) {
    float x = adcBuf[i];
    if (x < mean - hyst) armed = true;
    if (armed && prev <= mean && x > mean) {
      float t = (i - 1) + (mean - prev) / (x - prev);
      if (n == 0) firstT = t;
      else {
        float p = t - prevT;
        if (p < minP) minP = p;
        if (p > maxP) maxP = p;
      }
      lastT = prevT = t;
      n++;
      armed = false;
    }
    prev = x;
  }
  periodsOK = (n < 3) || (maxP / minP < 1.5f);
  return n;
}

static void analyze(uint32_t fs, Analysis &a) {
  /* basic statistics */
  uint32_t sum = 0;
  uint16_t mn = 65535, mx = 0;
  for (int i = 0; i < N_SAMPLES; i++) {
    sum += adcBuf[i];
    if (adcBuf[i] < mn) mn = adcBuf[i];
    if (adcBuf[i] > mx) mx = adcBuf[i];
  }
  float mean = (float)sum / N_SAMPLES;
  float vppCounts = mx - mn;

  float acc = 0;
  for (int i = 0; i < N_SAMPLES; i++) {
    float d = adcBuf[i] - mean;
    acc += d * d;
  }
  float rms  = sqrtf(acc / N_SAMPLES);
  float peak = fmaxf(mx - mean, mean - mn);

  a.mean  = mean * VREF / ADC_MAX;
  a.vpp   = vppCounts * VREF / ADC_MAX;
  a.rms   = rms * VREF / ADC_MAX;
  a.crest = (rms > 1.0f) ? peak / rms : 0;

  /* slope-sign fraction: ramp ~1.0 (or 0.0), triangle/sine ~0.5 */
  float dead = vppCounts * 0.02f;
  int pos = 0, neg = 0;
  for (int i = 4; i < N_SAMPLES - 4; i += 2) {
    float s1 = (adcBuf[i-4] + adcBuf[i-3] + adcBuf[i-2] + adcBuf[i-1]) * 0.25f;
    float s2 = (adcBuf[i]   + adcBuf[i+1] + adcBuf[i+2] + adcBuf[i+3]) * 0.25f;
    float d = s2 - s1;
    if (d >  dead) pos++;
    else if (d < -dead) neg++;
  }
  a.posFrac = (pos + neg > 0) ? (float)pos / (pos + neg) : 0.5f;

  /* frequency from crossings */
  float firstT, lastT;
  a.crossings = countCrossings(mean, vppCounts * 0.05f, firstT, lastT,
                               a.periodsOK);
  a.freq = (a.crossings >= 2) ? fs * (a.crossings - 1) / (lastT - firstT) : 0;

  /* classification by crest factor, ramp/triangle split by slope fraction */
  if (a.vpp < NO_SIGNAL_VPP) { a.type = "NO INPUT"; a.freq = 0; return; }
  if (a.crossings < 2)       { a.type = "OTHER";               return; }
  float cf = a.crest;
  if      (cf < 1.20f) a.type = "SQUARE";                      // ideal 1.000
  else if (cf < 1.55f) a.type = "SINE";                        // ideal 1.414
  else if (cf < 2.00f)                                         // ideal 1.732
    a.type = (a.posFrac > 0.60f || a.posFrac < 0.40f) ? "RAMP" : "TRIANGLE";
  else                 a.type = "OTHER";
}

/* Run capture at successive rates until enough clean cycles are seen */
static void acquireSignal(Analysis &a, uint32_t &usedFs) {
  for (int r = 0; r < N_RATES; r++) {
    usedFs = RATES[r];
    if (!capture(RATES[r])) { a.type = "ADC ERR"; return; }
    analyze(RATES[r], a);
    if (a.vpp < NO_SIGNAL_VPP) return;                    // nothing connected
    if (a.crossings >= MIN_CROSSINGS && a.periodsOK) return;
  }
}

/* ============================ TEMPERATURE =============================== */

/* ADC counts -> thermistor ohms -> deg C via lookup table interpolation */
static float readTemperature() {
  uint16_t raw = adcReadThermistorRaw();
  if (raw < 20 || raw > 4075) return NAN;                 // open/short sensor
  float v = raw * VREF / ADC_MAX;
  float rNtc = NTC_FIXED_R * v / (VREF - v);              // NTC on bottom leg

  if (rNtc >= NTC_RES[0])              return NTC_TEMP[0];            // <0 C
  if (rNtc <= NTC_RES[NTC_POINTS - 1]) return NTC_TEMP[NTC_POINTS-1]; // >100 C
  for (int i = 1; i < NTC_POINTS; i++) {
    if (rNtc > NTC_RES[i]) {                              // between i-1 and i
      float frac = (NTC_RES[i - 1] - rNtc) / (NTC_RES[i - 1] - NTC_RES[i]);
      return NTC_TEMP[i - 1] + frac * (NTC_TEMP[i] - NTC_TEMP[i - 1]);
    }
  }
  return NAN;
}

/* ============================ GAIN SENSE ================================ */

/* Returns index 0-3 of the single active selector line,
 * -1 = none active, -2 = more than one active (wiring fault). */
static int readGainSetting() {
  int found = -1;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(GAIN_PINS[i]) == HIGH) {
      if (found >= 0) return -2;
      found = i;
    }
  }
  return found;
}

/* ============================== DISPLAY ================================= */
/* Values are drawn with (fg, bg) color pairs and space-padding so they
 * overwrite cleanly without flicker — no full-screen redraws in the loop.  */

#define COL_BG      TFT_BLACK
#define COL_HEADER  0x0210        // dark navy
#define COL_LABEL   TFT_CYAN
#define COL_VALUE   TFT_WHITE
#define COL_ALERT   TFT_RED
#define COL_OK      TFT_GREEN
#define X_RIGHT     315           // right column start

static void drawStaticUI() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, 480, 36, COL_HEADER);
  tft.setTextColor(TFT_WHITE, COL_HEADER);
  tft.setTextSize(3);
  tft.setCursor(12, 7);
  tft.print("SIGNAL ANALYZER");
  tft.drawFastVLine(300, 44, 268, TFT_DARKGREY);

  tft.setTextSize(2);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setCursor(20, 52);        tft.print("SIGNAL TYPE");
  tft.setCursor(20, 140);       tft.print("FREQUENCY");
  tft.setCursor(20, 210);       tft.print("AMPLITUDE (Vpp)");
  tft.setCursor(20, 268);       tft.print("DC OFFSET");
  tft.setCursor(X_RIGHT, 52);   tft.print("OP-AMP TEMP");
  tft.setCursor(X_RIGHT, 160);  tft.print("GAIN");
}

/* Print a padded value so leftovers from longer strings get erased */
static void drawValue(int x, int y, int size, const char *s, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, COL_BG);
  tft.setCursor(x, y);
  tft.print(s);
  tft.print("   ");                                        // erase tail
}

static void showSignal(const Analysis &a) {
  bool noInput = (strcmp(a.type, "NO INPUT") == 0);
  drawValue(20, 82, 4, a.type, noInput ? COL_ALERT : COL_OK);

  char buf[24];
  if (noInput) {
    drawValue(20, 168, 3, "---", TFT_DARKGREY);
    drawValue(20, 236, 3, "---", TFT_DARKGREY);
    drawValue(20, 292, 2, "---", TFT_DARKGREY);
    return;
  }
  /* frequency, auto Hz/kHz */
  if (a.freq < 1000) snprintf(buf, sizeof buf, "%.1f Hz", a.freq);
  else               snprintf(buf, sizeof buf, "%.3f kHz", a.freq / 1000.0f);
  drawValue(20, 168, 3, buf, COL_VALUE);

  /* amplitude & offset referred back to the real input via divider math */
  snprintf(buf, sizeof buf, "%.2f V", a.vpp / DIV_K);
  drawValue(20, 236, 3, buf, COL_VALUE);
  snprintf(buf, sizeof buf, "%+.2f V", (a.mean - DIV_OFFSET) / DIV_K);
  drawValue(20, 292, 2, buf, COL_VALUE);
}

static void showTemperature(float tC) {
  char buf[16];
  if (isnan(tC)) drawValue(X_RIGHT, 84, 4, "ERR", COL_ALERT);
  else {
    snprintf(buf, sizeof buf, "%.1fC", tC);   // no space: keeps size-4 text on screen
    drawValue(X_RIGHT, 84, 4, buf, tC > 80 ? COL_ALERT : COL_VALUE);
  }
}

static void showGain(int idx) {
  char buf[16];
  if      (idx == -1) drawValue(X_RIGHT, 192, 4, "--", TFT_DARKGREY);
  else if (idx == -2) drawValue(X_RIGHT, 192, 4, "ERR", COL_ALERT);
  else {
    snprintf(buf, sizeof buf, "x%u", GAIN_VALUES[idx]);
    drawValue(X_RIGHT, 192, 4, buf, COL_OK);
    snprintf(buf, sizeof buf, "SW%d", idx + 1);
    drawValue(X_RIGHT, 232, 2, buf, COL_LABEL);
    return;
  }
  drawValue(X_RIGHT, 232, 2, " ", COL_BG);                 // clear SW label
}

/* =============================== MAIN =================================== */

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Instrument panel starting...");

  for (int i = 0; i < 4; i++) pinMode(GAIN_PINS[i], INPUT_PULLDOWN);

  tft.init();
  tft.setRotation(1);                                      // 480 x 320
  tft.setTextWrap(false);          // padding spaces must never wrap to next line
  drawStaticUI();

  adcBaseInit();
}

void loop() {
  static uint32_t lastTemp = 0, lastGain = 0;
  static int prevGain = -99;

  /* --- signal: capture + classify (dominates loop time, ~10 ms-1 s) --- */
  Analysis a = {0};
  uint32_t fs;
  acquireSignal(a, fs);
  showSignal(a);

  Serial.print(a.type);        Serial.print("  f=");
  Serial.print(a.freq, 1);     Serial.print(" Hz  CF=");
  Serial.println(a.crest, 3);

  /* --- temperature every 500 ms --- */
  if (millis() - lastTemp > 500) {
    lastTemp = millis();
    showTemperature(readTemperature());
  }

  /* --- gain selector every 200 ms, redraw only on change --- */
  if (millis() - lastGain > 200) {
    lastGain = millis();
    int g = readGainSetting();
    if (g != prevGain) { showGain(g); prevGain = g; }
  }

  /* heartbeat dot in the header */
  static bool beat = false;
  beat = !beat;
  tft.fillCircle(458, 18, 8, beat ? COL_OK : COL_HEADER);

  delay(50);
}
