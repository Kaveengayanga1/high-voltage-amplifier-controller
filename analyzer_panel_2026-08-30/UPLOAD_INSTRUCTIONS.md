# Upload Instructions — analyzer_panel rev 3 — 2026-08-30

This folder is self-contained and dated. If it conflicts with anything older
(pin-setup.md, earlier User_Setup.h, instrument_panel), **this folder wins**.

## Files here

| File | Purpose |
|---|---|
| `analyzer_panel_2026-08-30.ino` | Main sketch (interface: waveform, freq, input V, gain mode, temp, scope) |
| `analyzer_types.h` | Shared types (must stay in this folder) |
| `User_Setup_2026-08-30.h` | TFT_eSPI pin config — must be copied into the library (step 1) |
| `UPLOAD_INSTRUCTIONS.md` | This file |

## Pin map rev 3 (2026-08-30)

PA0 signal ADC · PA1 thermistor ADC · PA2–PA5 gain mode inputs · PA6 fan PWM (reserved)
PB0–PB7 LCD data · PB9 CS · PB10 DC · PB12 RST · PB13 WR · PB14 RD · 5V/GND to shield

## Step 1 — Install the display config (do this EVERY time the pin map changes)

Copy `User_Setup_2026-08-30.h` **over**:

```
C:\Users\kavee\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
```

(Keep the filename `User_Setup.h` at the destination.) Verify
`User_Setup_Select.h` in the same folder has `#include <User_Setup.h>` active.
TFT_eSPI ONLY reads the library copy — the file in this folder is just the master.

## Step 2 — Arduino IDE settings

- Board: **Generic STM32F4 series** → Board part number: **Generic F411CEUX**
- Upload method: **STM32CubeProgrammer (SWD)**
- USART support: Enabled (generic Serial) — for the 115200 debug output
- Open sketch: `analyzer_panel_2026-08-30.ino` (IDE loads the whole folder)

## Step 3 — Flash

1. Connect ST-Link V2 (SWDIO/SWCLK/GND/3.3V as usual).
2. Upload. Takes ~3 s. If "no target": hold NRST, click Upload, release.

## Step 4 — First power-up (display only, nothing on PA0–PA5)

Expected screen: header "SIGNAL ANALYZER rev3 2026-08-30", labels, empty scope box,
WAVEFORM = NO SIGNAL, FREQ = ---, GAIN MODE = NONE, TEMPERATURE = SENSOR?.
That is correct with nothing connected.

## Step 5 — Connect peripherals (power off between steps)

1. **Thermistor:** 3.3V → 10k → PA1 → NTC → GND (+100nF PA1→GND).
   Temperature should read ~room temp. Update `THERM_R25/BETA/RSERIES` in the
   sketch with your measured values.
2. **Gain lines:** amplifier gain-mode outputs → PA2..PA5. Default expects
   **active HIGH** (3.3V = selected); if yours are active LOW, set
   `#define GAIN_ACTIVE_HIGH 0`. ⚠ Lines must be 0–3.3V logic — never 5V or ±V.
3. **Signal:** FG set to 3 Vpp, +1.5 V offset (0–3 V unipolar), ≤1 kHz → PA0
   (until the divider/protection network from WIRING.md is built).
   When the divider is added, set `#define INPUT_ATTEN 7.8f` so the display
   shows true input volts.

## Step 6 — Verify

Serial monitor @115200 shows: fs (~40–50k), vpp, crest, sym, gain, type, freq, T.
500 Hz sine → SINE, crest≈1.41. Square → SQUARE, crest≈1.0.
Note: with blocking analogRead, classification is reliable up to ~4–5 kHz only.
The DMA upgrade (next step of the project) extends this to the full 11 kHz.

## Known-good history

- 2026-08-30 rev 3: data bus PB0–PB7, ADC PA0/PA1, gain PA2–PA5. ← CURRENT
- (older) rev 2: data PA0–PA7, WR PB13/RD PB14, ADC PB0/PB1 — obsolete
- (older) rev 1: WR PB1/RD PB0 — obsolete, no free ADC pins
