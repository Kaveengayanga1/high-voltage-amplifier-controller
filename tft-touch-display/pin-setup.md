You're right — good catch. The **WeAct Black Pill (STM32F411CEU6)** doesn't expose PB11 on its pin headers. This is a known quirk of that board: the STM32F411CEU6 chip package (UFQFPN48) simply doesn't have PB11 as a physical pin. PB10 and PB12 are both present, but PB11 is skipped.

Let me update the pin mapping. We just need to move CS to another free pin.

## Updated pin mapping

| Shield pin | Black Pill pin | Purpose |
|---|---|---|
| LCD_RST | PB12 | Reset |
| LCD_CS | **PB9** | Chip select (changed) |
| LCD_RS | PB10 | Register select (Data/Command) |
| LCD_WR | PB1 | Write strobe |
| LCD_RD | PB0 | Read strobe |
| LCD_D0 | PA0 | Data bit 0 |
| LCD_D1 | PA1 | Data bit 1 |
| LCD_D2 | PA2 | Data bit 2 |
| LCD_D3 | PA3 | Data bit 3 |
| LCD_D4 | PA4 | Data bit 4 |
| LCD_D5 | PA5 | Data bit 5 |
| LCD_D6 | PA6 | Data bit 6 |
| LCD_D7 | PA7 | Data bit 7 |

**PB9 is a safe choice** — it's exposed on the Black Pill headers, has no special peripheral conflicts for our purposes, and is close to the other control pins for clean wiring.

## Updated User_Setup.h

Just change the `TFT_CS` line in the config I gave earlier:

```cpp
// Control pins
#define TFT_RST   PB12
#define TFT_CS    PB9    // Changed from PB11 (not available on Black Pill)
#define TFT_DC    PB10
#define TFT_WR    PB1
#define TFT_RD    PB0
```

Everything else stays the same — data bus on PA0–PA7, same test sketch, same upload procedure.

## Quick reference: pins NOT available on WeAct Black Pill F411CE

For future reference, the STM32F411CEU6 in the UFQFPN48 package doesn't have these physical pins at all:

- **PB11** (the one you just hit)
- PC0–PC12 (only PC13, PC14, PC15 exist, and PC14/PC15 are wired to the 32.768 kHz crystal)
- PD0–PD15, PE0–PE15, PF, PG, PH0/PH1 headers not brought out (PH0/PH1 go to the 25 MHz HSE crystal)

The **fully usable GPIOs** on the WeAct Black Pill are: PA0–PA15, PB0–PB10, PB12–PB15, plus PC13 (onboard LED, usable but shares with LED). That's 30 GPIOs to work with — comfortable for most projects.

Also worth noting for your future projects: **PA11 and PA12 are USB D− and D+** — they work as GPIOs but you lose USB. **PA13 and PA14 are SWD** — usable as GPIOs but you lose the ability to debug/flash via ST-Link.

Update the one line in User_Setup.h, wire CS to PB9 instead of PB11, and try the flash. Everything else is unchanged.