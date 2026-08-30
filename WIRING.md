# Wiring Guide — Signal Analyzer (FINAL Pin Map rev 3 — supersedes ALL earlier tables)

## 1. Complete pin map

| Black Pill pin | Function |
|---|---|
| PA0 | Signal input (ADC1_IN0) |
| PA1 | Thermistor (ADC1_IN1) |
| PA2 | GAIN control 1 |
| PA3 | GAIN control 2 |
| PA4 | GAIN control 3 |
| PA5 | GAIN control 4 |
| PA6 | Fan PWM (TIM3_CH1) |
| PB0 | LCD_D0 |
| PB1 | LCD_D1 |
| PB2 | LCD_D2 (BOOT1 — on-board pull-down, OK) |
| PB3 | LCD_D3 |
| PB4 | LCD_D4 |
| PB5 | LCD_D5 |
| PB6 | LCD_D6 |
| PB7 | LCD_D7 |
| PB9 | LCD_CS |
| PB10 | LCD_RS (DC) |
| PB12 | LCD_RST |
| PB13 | LCD_WR |
| PB14 | LCD_RD |
| 5V / GND | LCD 5V / GND |

The library file `Documents\Arduino\libraries\TFT_eSPI\User_Setup.h` must match: data PB0–PB7 with `STM_PORTB_DATA_BUS`, control per table. Copy the project `User_Setup.h` over it.

Note: KEY button = PA0 = signal input. Pressing it just reads 0 V momentarily — harmless.

## 2. Thermistor → PA1

```
3.3V ──[ Rseries ]──●── PA1
                    │
                  [ NTC ]      100nF from PA1 to GND
                    │          (mount close to the pin)
                   GND
```

- Rseries: use a value close to your NTC's R25 (10k for a 10k NTC) — maximizes sensitivity mid-range. Measure its actual value with a DMM and put it in `THERM_RSERIES`.
- Update `THERM_R25` and `THERM_BETA` in the sketch with your confirmed values.
- Keep leads short; twist the thermistor pair if it runs any distance.

## 3. Signal input → PA0

Function generator (assume up to ±10 V) → scale + bias + protect:

```
FG out ──[ R1 68k ]──●──[ R3 1k ]──●── PA0
                     │             │
                  [ R2 10k ]    BAT54S (or 2x 1N5819):
                     │          one diode PA0→3.3V,
                    1.65V bias  one diode GND→PA0
                     │          + 100nF PA0→GND
```

Simplest working version (bipolar input):

- **Divider:** R1 = 68k from FG to node, R2 = 10k node to GND → ÷7.8. ±10 V in → ±1.28 V at node.
- **Bias to mid-rail:** DC-block with a 1 µF film/ceramic cap between divider node and the bias node, then bias node held at 1.65 V by two 100k resistors (3.3V→node→GND). Signal then swings 1.65 ± 1.28 V = 0.37–2.93 V. ✔ inside 0–3.3 V.
  - High-pass corner: f = 1/(2π·1µF·50k) ≈ 3.2 Hz — fine for 10 Hz minimum.
- **Protection:** 1k series (R3) into PA0, BAT54S clamping to 3.3V and GND, 100nF to GND at the pin.

Full chain:

```
FG ──68k──●──1µF──●──────1k──●── PA0
          │       │          ├─ BAT54S to 3.3V & GND
         10k     100k to 3.3V└─ 100nF to GND
          │      100k to GND
         GND
```

If your FG output stays within 0 to +3 V (unipolar), skip the cap + bias: just 68k/10k divider → 1k → PA0 with clamps.

## 4. Verification checkpoints

1. Before connecting PA0/PA1 to the MCU, power the analog circuits and verify with DMM:
   - Thermistor node ≈ 1.65 V at room temp (10k NTC + 10k series)
   - Signal bias node = 1.65 V with FG off
2. FG on, 1 kHz sine 5 Vpp → scope the PA0 node: centered ~1.65 V, amplitude ~0.32 Vp, never outside 0–3.3 V. Crank FG to max output and confirm clamps hold it within −0.3/+3.6 V.
3. Flash sketch, open Serial @115200 — check fs (~40–50 kSPS), crest ≈1.414 for sine, freq reads ~1000 Hz.
4. Warm the NTC with fingers — temp should rise smoothly.
```
