Good move — ST-Link is genuinely a much better experience. Let me get you set up.

## Exiting DFU mode

Simple: **just tap the NRST button once.** That's it.

The chip resets and boots from flash (running whatever sketch is currently there). DFU mode is only entered when BOOT0 is held high during reset — a normal reset without holding BOOT0 goes straight to your program.

Alternative: **unplug the USB cable, plug it back in.** Same effect — power-on reset boots from flash normally.

You don't need to do anything special to "quit" DFU mode. The BOOT0 button isn't a latching switch; the mode only persists until the next reset.

## ST-Link V2 setup

### Step 1: Identify the SWD pins

The Black Pill has a **4-pin SWD header on the short edge** of the board (opposite the USB-C connector). Looking at the board with USB-C facing you, it's the row of 4 pins at the far end. They're labeled on the silkscreen:

- **3V3** (3.3V)
- **DIO** (SWDIO)
- **CLK** (SWCLK)
- **GND**

Order varies slightly by board revision, but the labels are always there — check the silkscreen.

### Step 2: Identify the ST-Link V2 pins

The typical cheap ST-Link V2 clone (silver/blue aluminum stick) has pins labeled on the case itself. The relevant ones:

- **3.3V**
- **SWDIO**
- **SWCLK**
- **GND**

Also present but not needed: 5V, RST, SWIM, and others. Ignore those.

### Step 3: Wire it up

Use the female-to-female jumper wires that came with the ST-Link (usually 4 wires in a ribbon):

| ST-Link V2 pin | Black Pill pin |
|---|---|
| 3.3V | 3V3 |
| SWDIO | DIO |
| SWCLK | CLK |
| GND | GND |

**Do not connect the ST-Link's 5V pin** — use 3.3V only. Connecting 5V to the Black Pill's 3V3 pin will damage the chip.

**USB-C cable to the Black Pill is optional** — the ST-Link powers the board through the 3.3V line. Leave it disconnected during ST-Link programming unless you specifically want USB Serial output at the same time.

### Step 4: Install ST-Link drivers on Windows

If you already installed STM32CubeProgrammer for the DFU attempt, the ST-Link drivers are already installed. To confirm:

1. Plug the ST-Link V2 into your PC's USB port.
2. Open **Device Manager**.
3. Look under **"Universal Serial Bus devices"** — you should see **"STMicroelectronics STLink dongle"** or similar, with no yellow warning.

If you see a yellow warning, download and install the driver from: https://www.st.com/en/development-tools/stsw-link009.html

Unlike DFU, **ST-Link drivers don't need Zadig** — ST provides proper signed drivers.

### Step 5: Test connection with STM32CubeProgrammer

Before Arduino, verify the hardware works:

1. With ST-Link connected to PC and Black Pill wired up:
2. Open **STM32CubeProgrammer**.
3. Top-right dropdown: select **ST-LINK** (not USB this time).
4. Click the refresh/rotate icon.
5. It should detect your ST-Link serial number in the "Serial number" field.
6. Click **Connect**.
7. You should see:
   - Connection status: green
   - Flash memory contents displayed on the left
   - Target info showing: F411CE, revision, flash size (512 KB)

If Connect succeeds, everything is wired correctly and you're ready to flash from Arduino.

### Step 6: Configure Arduino IDE

Only one setting changes from your DFU setup:

- **Tools → Upload method → STM32CubeProgrammer (SWD)** ← this is the key change

Everything else stays the same:

- Board: **Generic STM32F4 series**
- Board part number: **BlackPill F411CE**
- U(S)ART support: **Enabled (generic 'Serial')**
- USB support: **CDC (generic 'Serial' supersede U(S)ART)** — if you also want USB Serial output
- Optimize: **Smallest (-Os default)**

### Step 7: Flash your sketch

1. Ensure ST-Link is connected to PC and to Black Pill.
2. In Arduino IDE, click **Upload** (Ctrl+U).
3. Watch the output pane at the bottom. You should see:
   ```
   Memory Programming ...
   Opening and parsing file: [your sketch].hex
   ...
   File download complete
   Time elapsed during download operation: 00:00:02.xxx
   RUNNING Program ...
   ```
4. **The board automatically resets and starts running your sketch.** No NRST tap needed.

That's it. One click, done. Every subsequent upload is the same — just click Upload.

## Comparison: DFU vs ST-Link workflow

| Step | DFU (USB-C) | ST-Link |
|---|---|---|
| Enter programming mode | Hold BOOT0 + tap NRST | (nothing) |
| Click Upload | ✓ | ✓ |
| Exit programming mode | Tap NRST | (nothing) |

The ST-Link workflow is genuinely one click.

## Troubleshooting

**"No STLink detected"** — ST-Link not connected to PC, or driver missing. Check Device Manager first. Try a different USB port.

**"Error: no debug probe detected"** — ST-Link plugged in but Windows didn't enumerate it. Unplug/replug the ST-Link. If persistent, try a different USB cable or port.

**"Cannot connect to target"** — ST-Link works, but can't talk to the Black Pill. Causes:
- SWDIO/SWCLK swapped — most common issue. Double-check the wires.
- 3.3V not connected — the Black Pill needs power. Check with multimeter that 3.3V pin reads ~3.3V.
- GND not connected — obvious but happens.
- Wire loose in the jumper — reseat all four wires firmly.

**"Target not halted"** — occasional issue where the chip is running code that interferes. Under "Tools → Optimize" try a different setting, or hold NRST while clicking Upload, releasing when programming starts. In CubeProgrammer, there's also a "Reset" dropdown (Hardware reset / Software reset / Core reset) — try "Hardware reset" if you have the NRST pin also wired (optional 5th wire).

**Upload succeeds but sketch doesn't run** — very rare with SWD. Check that Arduino's "Upload method" is set to SWD, not DFU. Also try tapping NRST once manually after upload.

**ST-Link firmware outdated warning** — CubeProgrammer sometimes complains the ST-Link firmware is old. Click "Upgrade" in CubeProgrammer's ST-Link firmware update tool. Takes 10 seconds, then works normally.

## Bonus: USB Serial still works

Even when flashing via ST-Link, you can still connect the USB-C cable *simultaneously* for `Serial.print` output:

- Plug in both USB-C (to PC) and ST-Link (also to PC)
- The Black Pill enumerates as a Serial COM port over USB-C
- Arduino Serial Monitor works on that COM port at 115200
- Flash via ST-Link → run sketch → see Serial output over USB-C

This is the ideal development setup: flash with one click via ST-Link, debug output flowing continuously over USB.

## To exit DFU mode right now

Just tap NRST on your Black Pill. It's out of DFU mode.

Then disconnect USB-C (or leave it — doesn't matter), wire up the ST-Link per the table above, change the Arduino upload method to SWD, and flash. You'll never miss the BOOT0/NRST dance.

Wire it up and try uploading your display test sketch. If any step throws an error, paste the exact message and we'll sort it.