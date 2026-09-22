# ChromaWOTD — User Guide

For someone who has the hardware and wants it running. If you want to change the firmware
itself, see [CONTRIBUTING.md](../CONTRIBUTING.md) instead.

---

## What it is

A 2.9" four-colour ePaper display (black / white / **red** / **yellow**) that shows:

- a **Bible verse** in the morning and a **word of the day** in the afternoon, chosen by the
  time of day
- the **local weather** as text in the footer row (temperature + condition)

It refreshes **three times a day** (06:00, 12:30, 18:00) and deep-sleeps in between, so a
small battery lasts a long time.

## What you need

| | |
|---|---|
| Board | Seeed **XIAO ESP32-S3** |
| Carrier | Seeed **ePaper Display Board EE05** |
| Panel | Seeed 2.9" **Quadruple Color** ePaper (BWRY), SKU `104990855` |
| Cable | USB-C, for flashing and charging |
| A phone or laptop | to run the one-time setup wizard |

> [!IMPORTANT]
> **There is no clock on the screen.** A 4-colour ePaper panel cannot refresh partially —
> every update is a full ~25 second pigment sweep — so this device is designed for
> glanceable content a few times a day, not for telling the time.

---

## 1. Install the firmware

### The easy way — flash a release image

1. Go to the [Releases page](https://github.com/JPTranter/ChromaWOTD/releases) and download
   `ChromaWOTD-vX.Y.Z.bin` (the **complete** image).
2. Install esptool once: `pip install esptool`
3. Plug the board in over USB-C and find its port:
   - Windows: `COM13`-style — check Device Manager, or `esptool.py --list`
   - Linux: `/dev/ttyACM0`
   - macOS: `/dev/cu.usbmodem*`
4. Flash the whole image at offset `0`:

   ```bash
   esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 ChromaWOTD-vX.Y.Z.bin
   ```

That's it — the image contains the bootloader, the partition table and the application.

> [!NOTE]
> **The device's USB port disappears while it is asleep.** That is normal: the ESP32-S3's
> native USB powers down in deep sleep. To wake it, **tap the RESET button twice**, or
> unplug/replug the cable. If the port never appears, hold **BOOT**, tap **RESET**, release
> **BOOT** to force ROM download mode. Both buttons are tiny and sit on the XIAO module
> beside the USB-C connector.

### Updating an already-flashed board

Download the **`-app.bin`** from the release instead and write it at `0x10000`:

```bash
esptool.py --chip esp32s3 --port <PORT> write_flash 0x10000 ChromaWOTD-vX.Y.Z-app.bin
```

This **keeps your Wi-Fi settings**, because they live in a separate flash region
(`0x9000`) that an app-only flash does not touch. The complete image at `0x0` spans that
region, so using it is the equivalent of a factory reset.

### Building it yourself

```bash
git clone https://github.com/JPTranter/ChromaWOTD.git
cd ChromaWOTD/firmware
pio run -e s3 -t upload
```

No credentials file is needed — a locally-built image is configured the same way as a
release image, over the setup portal. If the upload finds no port, the board is asleep:
`python tools/flash_when_awake.py --seconds 420` waits for it and flashes on sight.

---

## 2. First-time setup (the wizard on the screen)

A freshly flashed device has **no Wi-Fi settings**, so on first boot it shows a setup
screen instead of content:

1. The panel shows a **Wi-Fi QR code**, the network name (`ChromaWOTD-XXXXXX`) and a
   **password**. The password is new on every boot and is never stored anywhere else, so
   read it off the screen.
2. **Scan the QR code** with your phone to join that network, or join it manually from your
   Wi-Fi settings using the password on the panel.
3. A configuration page opens by itself. (If it does not, browse to `http://192.168.4.1`.)
   Enter:
   - your **Wi-Fi network name and password**
   - your **timezone** (picked from a list)
   - your **latitude and longitude**
   - optionally a device name, and which content you want
4. Press **Save and restart**. The device reboots, joins your network and draws its first
   real screen.

> [!TIP]
> The setup window lasts **10 minutes**. If it closes before you finish, the device sleeps
> and you can simply press any button to bring the setup screen back.

---

## 3. Using it day to day

### What appears, and when

| Time | Content | Weather column |
| :--- | :--- | :--- |
| 00:00 – 14:59 | **Verse of the Day** | today's high + condition |
| 15:00 – 23:59 | **Word of the Day** (or the verse, if the source has not published today's edition yet — see below) | today's high or tomorrow's |
| 18:00 – 23:59 | **Word of the Day** | **tomorrow's** high, labelled `TOMORROW` |

Nothing is downloaded on a button press — the device wakes on its own at those three times.

> **Why the word only appears from 15:00:** A.Word.A.Day publishes its next word at 00:01 US
> Eastern time, which is 14:01 in Melbourne (15:01 in daylight saving, and 16:01 when Melbourne is
> on daylight saving while the US is not) — so a midday refresh would still be showing yesterday's
> word. The word window therefore opens at 15:00: the 06:00 and 12:30 refreshes show the verse, and
> the word appears at 18:00. If a refresh ever does catch a word the source has not yet replaced,
> the device reads the edition date printed on the page and shows the verse rather than repeating
> yesterday's word.

### The buttons

All three side buttons do the same thing:

| Gesture | What happens |
| :--- | :--- |
| **Tap** | Wakes up and refreshes immediately |
| **Hold ~1 second** | Shows the **other** content (verse ↔ word) for this refresh |
| **Hold 10 seconds** | **Factory reset** — erases the Wi-Fi settings and returns to the setup wizard |

A tap is not instant: each refresh is a full ~25 second pigment sweep, so give it half a
minute.

> A hold *overrides* the clock for that refresh only. The next scheduled wake goes back to
> the time-of-day rule, so the device can never get stuck showing the wrong thing.

### Reading the screen

- **Yellow band (top)** — what you are reading: the verse's citation (`Proverbs 3:5-6`) or
  the word with its pronunciation (`breviloquent (bre-VIL-uh-kwuhnt)`), plus the date.
- **Body** — the verse or definition, as large as it will fit.
- **Footer row** — anything that went wrong sits on the **left in red**; the weather sits on
  the **right**.

Colour is meaningful, not decorative: **red** marks citations, alerts and highlighted
phrases; **yellow** is the header band; everything else is black on white.

### Changing your settings later

Hold any button for **10 seconds** to wipe the settings and bring the setup wizard back,
then run through section 2 again. This is also how you move the device to a different
Wi-Fi network.

---

## 4. Troubleshooting

| Symptom | What is happening |
| :--- | :--- |
| The serial port is missing when you plug in | The device is deep-asleep and its USB port is powered down. Tap RESET twice, or unplug/replug. |
| The upload says "port is busy" | Something else on your computer is holding the port (a serial monitor, or another esptool). Close it. |
| The screen has not changed for hours | It is asleep. That is normal between the three daily slots — tap a button to refresh. |
| `OFFLINE: no wifi` on the panel | The device could not join Wi-Fi. Check the network name/password by re-running the setup wizard. |
| `PARTIAL: <api> failed` on the panel | The network is fine but one source did not respond. It retries at the next scheduled slot. |
| The panel shows "Verse/Word unavailable" | The content could not be fetched. The device deliberately shows **nothing invented** rather than a stale verse pretending to be today's. |
| The device asks to be set up again | Its stored settings were lost. Re-run the wizard. (The cause is a known limitation — see below.) |

---

## 5. Known limitations

- **No clock face.** The panel cannot refresh quickly enough (see the note at the top).
- **No OTA updates.** Updating means plugging in USB-C and flashing the `-app.bin`.
- **Every WMO condition is spelled out in words**, so the panel is never ambiguous about
  what the weather is doing.
- **Settings can, rarely, be lost.** The stored configuration shares a small flash region
  with the Wi-Fi stack; if that region runs out of room the chip reformats it. The firmware
  now keeps its own settings in a separate region, but a device offered the setup wizard
  again after a long service life is this, not a crash.

---

## Where to go next

| | |
| :--- | :--- |
| How the display has changed over time | [docs/UI_HISTORY.md](UI_HISTORY.md) |
| Architecture, data flow, power sequence | [docs/ARCHITECTURE.md](ARCHITECTURE.md) |
| Hard-won findings and hardware quirks | [docs/lessons/LESSONS_LEARNT.md](lessons/LESSONS_LEARNT.md) |
| Building, testing and contributing | [CONTRIBUTING.md](../CONTRIBUTING.md) |
| Current state and what is next | [docs/STATUS.md](STATUS.md) |
