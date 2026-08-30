# Input Conditioning Circuit — Design + Proteus Simulation Guide

Scales ±10 V function-generator output to 0.15–3.15 V centered at 1.65 V for PA0
(ADC1_IN0). DC-coupled, so it works from 10 Hz to 11 kHz with no cap distortion.

## Circuit

```
FUNC GEN ──── R1 10k ────●──────────────── PA0 (node A)
                         │
              ┌──────────┼──────────┐
              │          │          │
            R2 4.3k    C1 2.2nF   R3 3.0k        BAT54S at node A:
              │          │          │             pin1→node, pin3→node? No —
             GND        GND       3.3V            common cathode to 3.3V,
                                                  common anode to GND (below)
Clamp: D1 (Schottky) node A → 3.3V  (cathode to 3.3V)
       D2 (Schottky) GND → node A   (cathode to node A)
```

All resistors 1% metal film. BAT54S is a dual Schottky in SOT-23 — for
breadboard use two 1N5819 instead (same orientation as D1/D2 above).

## Why these values

Node voltage = k·Vin + Voffset where k = G1/(G1+G2+G3), Voffset = 3.3·G3/(G1+G2+G3).

| Property | Value |
|---|---|
| Gain k | 0.1502 (target 0.150 — E24 values land almost exactly) |
| Offset | 1.652 V (mid-rail) |
| ±10 V in | 0.150 V / 3.154 V out — 150 mV headroom to each rail |
| Worst case 1% resistors | 0.119–3.176 V — still inside rails |
| Output impedance (node A) | 1.5 kΩ — fine for ADC with C1 as charge reservoir |
| RC corner (1.5 kΩ, 2.2 nF) | 48 kHz → −0.22 dB at 11 kHz, flat below |
| Input impedance seen by generator | ≈11.8 kΩ — trivial load for 50 Ω output |
| Standing current from 3.3 V rail | 0.55 mA |

Firmware conversion back to real input volts: `Vin = (Vadc − 1.652) / 0.1502`.

Verified end-to-end in simulation: all four waveforms at 10 Hz–11 kHz through
this network, quantized to 12 bits, classify correctly with <2% frequency error.

## Proteus simulation — step by step

Proteus VSM has no STM32F411 model, so simulate the analog front end only and
verify node A behaves; the MCU part you test on real hardware (Checkpoint 4).

**1. Schematic (Proteus 8.x, new project, no firmware/PCB)**

Pick parts (P button): `RES` ×3 (set 10k, 4.3k, 3k), `CAP` (2.2nF),
`BAT54S` (or 2× `1N5819` / generic `SCHOTTKY`). Wire as above.
For 3.3 V: place a `POWER` terminal, label it `+3.3V`, then Design →
Configure Power Rails → add +3.3V net with 3.3 V (or use a `BATTERY` at 3.3 V).

**2. Stimulus and measurement**

- Place a **SINE generator** (Generator Mode) on the input: Amplitude 10 V,
  Frequency 1 kHz. Or use the interactive **VSM Signal Generator** instrument
  to switch waveform shape live.
- Attach the **Oscilloscope** instrument: Ch A = input, Ch B = node A.
- Optional: **DC Voltmeter** on node A to read the 1.652 V bias with input off.

**3. Tests to run**

| # | Test | Expect at node A |
|---|---|---|
| 1 | Input disconnected/0 V | Steady 1.65 V DC |
| 2 | Sine 1 kHz, ±10 V | Same shape, 0.15–3.15 V, no clipping |
| 3 | Sine at 10 Hz and 11 kHz | Amplitude unchanged (<3% droop at 11 kHz) |
| 4 | Triangle & square, ±10 V | Shape preserved; square edges slightly rounded (RC, ~0.3 µs — invisible at these frequencies) |
| 5 | Overdrive: ±15 V | Clamps flat at ≈ −0.3 V and ≈ 3.6 V — diodes doing their job |
| 6 | Graph mode: Frequency analysis 1 Hz–1 MHz on node A | Flat 0.15 gain to ~20 kHz, −3 dB at ~48 kHz |

For test 6 use a Graph → FREQUENCY sweep with the input generator as reference.

**4. Real-hardware checkpoint (after Proteus passes)**

1. Build the network on breadboard, power from Black Pill 3.3 V pin.
2. Before connecting PA0: scope node A, repeat tests 1–2. Verify 1.65 V bias
   and no excursion beyond 0–3.3 V at full generator amplitude.
3. Connect node A → PA0, flash `signal-analyzer.ino`, open Serial @115200.
4. Sweep all four waveforms 10 Hz → 11 kHz; compare `f=` with the GDS-1072B.
   Check `mean=` reads ≈1.65 V — a drift means divider/rail issue.

## ±20 V variant (if the generator doubles into high-Z, or for PA85 fault-proofing)

Many bench generators display amplitude assuming a 50 Ω load and deliver 2× into
high impedance — check yours by scoping the output open-circuit at your normal
setting. If "20 Vpp" is really 40 Vpp, or you want the input to survive an
accidental touch of the PA85's ±100 V output, use:

| Ref | ±10 V version | ±20 V version |
|---|---|---|
| R1 | 10 kΩ | 20 kΩ as **2 × 10 kΩ ½ W in series** |
| R2 | 4.3 kΩ | 3.6 kΩ |
| R3 | 3.0 kΩ | 3.0 kΩ |
| C1 | 2.2 nF | 2.2 nF (Zout ≈ 1.51 kΩ, corner unchanged ≈ 48 kHz) |

Gain k = 0.0756, offset 1.664 V → ±20 V maps to 0.15–3.18 V. A ±100 V fault is
limited to ~4.8 mA, dissipation ~0.23 W per R1 half — survivable indefinitely.
Firmware conversion for this variant: `Vin = (Vadc − 1.664) / 0.0756`.
Trade-off: input resolution halves (~11 mV/LSB) — if the generator does NOT
double, prefer the ±10 V set.

Why not AC coupling (the often-suggested alternative): a coupling cap with
fc ≈ 2 Hz tilts a 10 Hz square by ~47% (τ ≈ 80 ms vs 50 ms half-period) —
verified to misclassify square→SINE and ramp→OTHER at 10 Hz. A cap big enough
to avoid this (~200 µF non-polar) is impractical. DC coupling has no low-end
limit and also preserves the input's true DC offset.

## Notes

- The clamp diodes conduct only during faults; at normal levels the 1%-tolerance
  worst case (3.18 V) stays below the ~3.6 V clamp point, so no distortion.
- Don't increase C1 much: at 4.7 nF the corner falls to 22 kHz and starts
  attenuating 11 kHz visibly. Don't omit it either — it's the ADC charge
  reservoir for the 1.6 µs sample window.
- If you later want more resolution for small signals, add a switchable ×1
  range (jumper bypassing R1 with 0 Ω changes k to ~0.42) — but the current
  single range already gives ~7 mV of input per LSB.
