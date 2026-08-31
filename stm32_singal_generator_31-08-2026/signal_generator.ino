/*
 * STM32F411 Black Pill Signal Generator
 *
 * PA8 -> 100 Hz sine wave
 *        Generated as 200 kHz PWM.
 *        Use RC low-pass filter for smooth sine.
 *
 * PB8 -> 200 Hz square wave
 *        Direct 0V / 3.3V GPIO output.
 *
 * Arduino IDE:
 * Board: Generic STM32F4 / BlackPill F411CE
 * STM32 core: STM32duino
 */

#include <Arduino.h>
#include <HardwareTimer.h>
#include <math.h>

// =====================================================
// Output pins
// =====================================================

#define SINE_PIN    PA8
#define SQUARE_PIN  PB8

// PA8 = TIM1 channel 1 on STM32F411
#define SINE_PWM_CHANNEL 1

// =====================================================
// Frequencies
// =====================================================

#define SINE_FREQ_HZ       100
#define SQUARE_FREQ_HZ     200

// PWM carrier used to synthesize sine
#define PWM_CARRIER_HZ     200000

// Number of samples per sine cycle
#define SINE_SAMPLES       256

// 100 Hz × 256 samples
#define SINE_UPDATE_HZ     (SINE_FREQ_HZ * SINE_SAMPLES)

// =====================================================
// Timers
// =====================================================

// TIM1 -> PWM carrier on PA8
HardwareTimer sinePWM(TIM1);

// TIM2 -> updates sine amplitude
HardwareTimer sineTimer(TIM2);

// TIM3 -> square-wave timing
HardwareTimer squareTimer(TIM3);

// =====================================================
// Sine lookup table
// =====================================================

uint16_t sineTable[SINE_SAMPLES];

volatile uint16_t sineIndex = 0;
volatile bool squareState = false;

// =====================================================
// 100 Hz sine update interrupt
// =====================================================

void sineUpdateISR()
{
  sinePWM.setCaptureCompare(
      SINE_PWM_CHANNEL,
      sineTable[sineIndex],
      RESOLUTION_12B_COMPARE_FORMAT
  );

  sineIndex++;

  if (sineIndex >= SINE_SAMPLES)
    sineIndex = 0;
}

// =====================================================
// 200 Hz square-wave interrupt
// =====================================================

void squareUpdateISR()
{
  squareState = !squareState;

  digitalWrite(SQUARE_PIN, squareState);
}

// =====================================================
// Setup
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("STM32F411 Signal Generator");
  Serial.println("--------------------------");
  Serial.println("PA8 : 100 Hz sine");
  Serial.println("PB8 : 200 Hz square");
  Serial.println();

  // ---------------------------------------------------
  // Generate 256-entry sine lookup table
  // ---------------------------------------------------

  for (int i = 0; i < SINE_SAMPLES; i++)
  {
    float angle =
        2.0f * PI * ((float)i / (float)SINE_SAMPLES);

    // sin() gives -1 ... +1
    // Convert to 0 ... 1
    float value =
        (sinf(angle) + 1.0f) * 0.5f;

    // Convert to 12-bit PWM value
    sineTable[i] =
        (uint16_t)(value * 4095.0f);
  }

  // ===================================================
  // Configure PA8 PWM
  // ===================================================

  sinePWM.pause();

  sinePWM.setMode(
      SINE_PWM_CHANNEL,
      TIMER_OUTPUT_COMPARE_PWM1,
      SINE_PIN
  );

  // 200 kHz PWM carrier
  sinePWM.setOverflow(
      PWM_CARRIER_HZ,
      HERTZ_FORMAT
  );

  // Start at middle voltage
  sinePWM.setCaptureCompare(
      SINE_PWM_CHANNEL,
      2048,
      RESOLUTION_12B_COMPARE_FORMAT
  );

  sinePWM.refresh();
  sinePWM.resume();

  // ===================================================
  // Configure sine sample-update timer
  // ===================================================

  sineTimer.pause();

  // 100 Hz × 256 = 25.6 kHz update rate
  sineTimer.setOverflow(
      SINE_UPDATE_HZ,
      HERTZ_FORMAT
  );

  sineTimer.attachInterrupt(
      sineUpdateISR
  );

  sineTimer.refresh();
  sineTimer.resume();

  // ===================================================
  // Configure 200 Hz square wave
  // ===================================================

  pinMode(SQUARE_PIN, OUTPUT);
  digitalWrite(SQUARE_PIN, LOW);

  squareTimer.pause();

  /*
   * Output toggles on every interrupt.
   *
   * 200 Hz square:
   *
   * LOW -> HIGH = one interrupt
   * HIGH -> LOW = another interrupt
   *
   * Therefore interrupt frequency = 400 Hz.
   */

  squareTimer.setOverflow(
      SQUARE_FREQ_HZ * 2,
      HERTZ_FORMAT
  );

  squareTimer.attachInterrupt(
      squareUpdateISR
  );

  squareTimer.refresh();
  squareTimer.resume();

  Serial.println("Outputs started.");
}

// =====================================================
// Main loop
// =====================================================

void loop()
{
  // Everything is generated using hardware timers.
}