/*
 * signal-analyzer.ino  —  Stage 1+2: DMA ADC capture, frequency, waveform class
 * STM32F411CEU6 Black Pill, Arduino-core-for-STM32 v2.12.0 (HAL used directly)
 *
 * Signal input: PA0 (ADC1_IN0), 0–3.3 V ONLY.
 *   No divider built yet -> set function generator to unipolar:
 *   e.g. 1.5 V offset, <=1.4 Vpk amplitude, through 1 kOhm series resistor.
 *
 * Output: Serial (115200) — frequency, Vpp, crest factor, classification.
 * LCD is NOT used in this stage (Stage 3 adds the UI).
 */

#include <Arduino.h>

// ---------------- capture configuration ----------------
#define N_SAMPLES 2048
// Sample rates tried fast->slow until enough cycles are seen in the window:
//   200 kHz : good for ~400 Hz .. 11 kHz (>=18 samples/cycle at 11 kHz)
//    20 kHz : good for ~40 .. 400 Hz
//     2 kHz : good for 10 .. 40 Hz (window = 1.02 s)
static const uint32_t RATES[] = { 200000, 20000, 2000 };
#define N_RATES 3
#define MIN_CROSSINGS 6          // rising mean-crossings needed to trust a rate

#define VREF 3.3f
#define ADC_MAX 4095.0f
#define NO_SIGNAL_VPP 0.08f      // below this Vpp -> "NO SIGNAL"

uint16_t adcBuf[N_SAMPLES];
volatile bool captureDone = false;

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim3;

// ---------------- HAL setup ----------------
static void adcInit() {
  __HAL_RCC_ADC1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_0;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;   // 100/4 = 25 MHz ADC clk
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;                // timer-paced, not free-running
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  HAL_ADC_Init(&hadc1);

  ADC_ChannelConfTypeDef s = {0};
  s.Channel = ADC_CHANNEL_0;                              // PA0
  s.Rank = 1;
  s.SamplingTime = ADC_SAMPLETIME_84CYCLES;               // 84+12 = 96 clk = 3.84 us
  // Long sample time suits the ~1.5 kOhm divider source impedance; still fits
  // the 5 us period at 200 kSPS.
  HAL_ADC_ConfigChannel(&hadc1, &s);

  hdma_adc1.Instance = DMA2_Stream0;                      // ADC1 -> DMA2 Stream0 Ch0
  hdma_adc1.Init.Channel = DMA_CHANNEL_0;
  hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode = DMA_NORMAL;                       // one-shot capture
  hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  HAL_DMA_Init(&hdma_adc1);
  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

// TIM3 update event = ADC trigger. APB1 timer clock on F411 @100 MHz = 100 MHz.
static void timerInit(uint32_t fs) {
  __HAL_RCC_TIM3_CLK_ENABLE();
  uint32_t timclk = 100000000UL;
  uint32_t psc = 0;
  uint32_t arr = timclk / fs;
  while (arr > 65536UL) { psc++; arr = timclk / ((psc + 1) * fs); }

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

extern "C" void DMA2_Stream0_IRQHandler(void) {
  HAL_DMA_IRQHandler(&hdma_adc1);
}

// Called by HAL when the DMA buffer is full
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h) {
  if (h->Instance == ADC1) {
    HAL_TIM_Base_Stop(&htim3);
    captureDone = true;
  }
}

// Blocking capture of N_SAMPLES at fs. Returns true on success.
static bool capture(uint32_t fs) {
  captureDone = false;
  timerInit(fs);
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adcBuf, N_SAMPLES) != HAL_OK) return false;
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

// ---------------- analysis ----------------
struct Analysis {
  float mean, vpp, rms, crest, posFrac, freq;
  int   crossings;
  bool  periodsOK;   // crossing intervals uniform -> trustworthy frequency
  const char *type;
};

// Rising mean-crossings with hysteresis; linear interpolation of crossing time.
// Also checks period uniformity: noise-induced crossings give ragged intervals.
static int countCrossings(float mean, float hyst, float &firstT, float &lastT,
                          bool &periodsOK) {
  int n = 0;
  bool armed = false;                 // set true once signal goes below mean-hyst
  float prev = adcBuf[0], prevT = 0;
  float minP = 1e9f, maxP = 0;
  firstT = lastT = 0;
  for (int i = 1; i < N_SAMPLES; i++) {
    float x = adcBuf[i];
    if (x < mean - hyst) armed = true;
    if (armed && prev <= mean && x > mean) {
      float t = (i - 1) + (mean - prev) / (x - prev);  // interpolated index
      if (n == 0) firstT = t;
      else {
        float p = t - prevT;
        if (p < minP) minP = p;
        if (p > maxP) maxP = p;
      }
      lastT = t;
      prevT = t;
      n++;
      armed = false;
    }
    prev = x;
  }
  periodsOK = (n < 3) || (maxP / minP < 1.5f);
  return n;
}

static void analyze(uint32_t fs, Analysis &a) {
  // mean, min, max
  uint32_t sum = 0;
  uint16_t mn = 65535, mx = 0;
  for (int i = 0; i < N_SAMPLES; i++) {
    sum += adcBuf[i];
    if (adcBuf[i] < mn) mn = adcBuf[i];
    if (adcBuf[i] > mx) mx = adcBuf[i];
  }
  float mean = (float)sum / N_SAMPLES;
  float vppCounts = mx - mn;

  // RMS about the mean
  float acc = 0;
  for (int i = 0; i < N_SAMPLES; i++) {
    float d = adcBuf[i] - mean;
    acc += d * d;
  }
  float rms = sqrtf(acc / N_SAMPLES);

  float peak = fmaxf(mx - mean, mean - mn);
  a.mean  = mean * VREF / ADC_MAX;
  a.vpp   = vppCounts * VREF / ADC_MAX;
  a.rms   = rms * VREF / ADC_MAX;
  a.crest = (rms > 1.0f) ? peak / rms : 0;

  // Fraction of significant slopes that are positive (4-sample boxcar smoothing).
  // Triangle/sine ~0.5, ramp -> near 1.0 (or 0.0 for falling saw).
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

  // frequency
  float firstT, lastT;
  float hyst = vppCounts * 0.05f;
  a.crossings = countCrossings(mean, hyst, firstT, lastT, a.periodsOK);
  a.freq = (a.crossings >= 2) ? fs * (a.crossings - 1) / (lastT - firstT) : 0;

  // ---------- classification ----------
  if (a.vpp < NO_SIGNAL_VPP)      { a.type = "NO SIGNAL"; a.freq = 0; return; }
  if (a.crossings < 2)            { a.type = "OTHER";     return; }

  float cf = a.crest;
  if      (cf < 1.20f)                  a.type = "SQUARE";    // ideal 1.0
  else if (cf < 1.55f)                  a.type = "SINE";      // ideal 1.414
  else if (cf < 2.00f) {
    // 0.60/0.40 (not 0.70/0.30): at 11 kHz only ~18 samples/cycle remain and
    // smoothing blurs the ramp's fast edge, pulling posFrac toward 0.5.
    // Genuine triangle stays at 0.50 +/- 0.02, so the margin is safe.
    if (a.posFrac > 0.60f || a.posFrac < 0.40f) a.type = "RAMP";     // ideal 1.732
    else                                        a.type = "TRIANGLE"; // ideal 1.732
  }
  else                                  a.type = "OTHER";     // spiky / noise
}

// ---------------- main ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Signal analyzer — Stage 1+2 (Serial only)");
  adcInit();
}

void loop() {
  Analysis a = {0};
  uint32_t usedFs = 0;
  bool ok = false;

  // Try fast->slow until enough cycles fit in the window
  for (int r = 0; r < N_RATES; r++) {
    if (!capture(RATES[r])) { Serial.println("capture timeout!"); return; }
    analyze(RATES[r], a);
    usedFs = RATES[r];
    if (a.vpp < NO_SIGNAL_VPP) { ok = true; break; }        // nothing there, stop
    // Accept this rate only if enough cycles AND uniform periods — noise on a
    // slow slope can fake crossings at the fast rate (e.g. 10 Hz ramp @ 200 kHz)
    if (a.crossings >= MIN_CROSSINGS && a.periodsOK) { ok = true; break; }
  }
  if (!ok && a.crossings >= 2) ok = true;  // slowest rate, few cycles — accept

  Serial.print(a.type);
  Serial.print("  f=");     Serial.print(a.freq, 1);   Serial.print(" Hz");
  Serial.print("  Vpp=");   Serial.print(a.vpp, 2);    Serial.print(" V");
  Serial.print("  mean=");  Serial.print(a.mean, 2);   Serial.print(" V");
  Serial.print("  CF=");    Serial.print(a.crest, 3);
  Serial.print("  posFrac=");Serial.print(a.posFrac, 2);
  Serial.print("  fs=");    Serial.print(usedFs / 1000.0f, 0); Serial.println(" kHz");

  delay(300);
}
