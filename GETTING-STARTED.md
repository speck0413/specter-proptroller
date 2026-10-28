# Getting started: from a new board to a running prop

This page takes you from the parts on your table to a prop that runs, with no programming. You will flash the
stock firmware, set up the sound card, wire one input and one output, and type a short effect script on your phone.

Words you may not know are in the [glossary](firmware/arduino/lib/proptroller/GLOSSARY.md).

No Proptroller board? The firmware also runs on a bare ESP32 DevKit, ESP32-S3 DevKit or ESP32-C3 SuperMini (on
its own or on a breakout board), with MOSFET or relay modules for the outputs: [firmware/BARE-DEVKIT.md](firmware/BARE-DEVKIT.md).

## 1. What you need

| Part | Notes |
|---|---|
| A Specter Proptroller board | Every board made so far is **rev A** (silkscreen "RELAYS: <=30V DC ONLY"): low-voltage DC only, never mains. |
| A microcontroller that plugs into it | **one** of: a 30-pin ESP32 DevKit (ESP32-WROOM-32, 4 MB flash), or a Raspberry Pi **Pico W** or **Pico 2 W** (both with Wi-Fi). A plain Pico works too but has no Wi-Fi and no phone page. |
| A USB cable | a data cable, not a charge-only one (most ESP32 DevKits and every Pico W / Pico 2 W take micro-USB). |
| A DFPlayer Mini | the MP3 player module that plugs into the board, if your prop plays sound. |
| A microSD card | 32 GB or smaller, formatted **FAT32** (section 3). |
| A speaker | 4-8 Ω, 3 W or less, on the SPK terminals (never connect either speaker wire to ground). |
| A power supply | a listed 12 V DC supply (the board takes 9-24 V) with its own over-current protection, **10 A or less**; 5 A or less on the barrel jack. |
| 3.5 mm plugs or cables | stereo (TRS) plugs for the TRIG and CONTACT jacks. |
| Your props | a button, step mat or motion sensor for a trigger; 12 V lamps, LED strips, fans or solenoids for the outputs. |

**What goes where.** OUT1-OUT4 switch the board's supply voltage: a 12 V supply gives 12 V on them, a 24 V supply
24 V, so buy 12 V parts and a 12 V supply. Each OUT carries up to 5 A on/off, but dimming, fading and flicker are
for small loads: at most 2 A on a 12 V supply (1 A on 24 V). The whole board carries 10 A. RLY1 / RLY2 are dry
contacts (like a hand pressing a button): up to 30 V DC, never mains. The board does not measure or limit current, so
keep each load within these ratings ([hardware/README.md](hardware/README.md) has all of them). Fog machines are
driven only through their **DMX IN** socket with the DMX add-on, never through a relay
([firmware/SHOW-SCRIPT.md](firmware/SHOW-SCRIPT.md) section 7).

## 2. Put the firmware on the microcontroller

Do this with the microcontroller **out of the board** (or the board unpowered), plugged into your computer by USB.
The files are on the [Releases page](https://github.com/speck0413/specter-proptroller/releases): open the newest
release and look under **Assets**. (Until the first release is published there are no ready-made files: build
them yourself with the [developer quick start](README.md#developer-quick-start-vs-code).)

### ESP32 DevKit: from the browser

1. Open the [browser flasher](https://speck0413.github.io/specter-proptroller/) in Chrome or Edge on a computer
   (not a phone; Firefox and Safari cannot talk to USB).
2. Click **Connect**, pick the port that appears when you plug the DevKit in (`CP210x` or `CH340`), and click
   **Install**. It takes about a minute.
3. No port appears? Install the USB driver for the chip next to the DevKit's USB socket (Silicon Labs CP210x or
   WCH CH340) and try another cable. If it cannot connect, hold the DevKit's **BOOT** button while you click
   Connect.

Without the browser: download `first-flash-esp32-devkit-<version>.bin` from the release and write it at address
0x0 with `esptool.py write_flash 0x0 first-flash-esp32-devkit-<version>.bin`.

### Pico W or Pico 2 W: copy one file

1. Download `first-flash-pico-w-<version>.uf2` (Pico 2 W: `first-flash-pico2-w-<version>.uf2`) from the release.
2. Hold the **BOOTSEL** button on the Pico while you plug in the USB cable, then let go. A drive called `RPI-RP2`
   (Pico 2 W: `RP2350`) appears.
3. Copy the file onto that drive. The Pico restarts by itself when the copy is done, and the drive disappears.

Later updates go over Wi-Fi from the board's page (Firmware tab), with the `generic-…bin` file for your board from
a newer release. Your settings, shows and script are kept.

## 3. The sound card

The sound player plays files by **number**, one sound per folder: sound 1 is the one file in the card's folder
`01` (`01/001-ambient.mp3`), sound 2 the one in folder `02`, and so on up to `99`. Let Studio's **SD card** tool
fill the card: it erases it, makes the folders and names the files. To do it by hand, follow
[docs/SD-CARD.md](docs/SD-CARD.md) ("Setting up the SD card for the DFPlayer").

In an effect script, `audio_single = 1` plays sound 1, `= 2` sound 2, and so on. Put the card in the DFPlayer
Mini before you power the board.

## 4. Wire it up

With the power **off**:

1. Plug the microcontroller into its sockets (the ESP32 DevKit **or** the Pico, never both): its antenna end
   towards the board edge, its USB socket towards the middle of the board, pin 1 where the silkscreen marks it
   ([hardware/README.md](hardware/README.md), "Orientation"). Plugged in the wrong way round it can be destroyed:
   check twice. Plug in the DFPlayer Mini with its card, its microSD slot facing the board edge.
2. **A trigger on TRIG1.** A button, a step mat or a motion sensor with a contact (relay) output: its two wires
   to the tip and the sleeve of a 3.5 mm plug. Closing them is a press.
   A **powered 3-wire sensor** can take its power from the ring, but only a sensor that runs on **3.3 V** and
   draws **15 mA or less** (for example a small 3.3 V PIR module such as the AM312; the common HC-SR501 wants
   about 5 V and does not run from the ring). Its signal goes on the tip and ground on the sleeve; then set
   **Settings → TRIG1 input type** to `sensor_high` on the board's page (step 5). In `sensor_high` an unplugged
   cable counts as a trigger: use the default `switch` type for anything safety-related.
3. **A light on OUT1.** A 12 V lamp or LED strip: its + wire to OUT1 +, its − wire to OUT1 −.
4. **A speaker** on SPK.
5. **Power:** the 12 V supply into the barrel jack (POWER IN). The microcontroller's own LED lights.

## 5. Open the board's page on your phone

1. On your phone, join the Wi-Fi network **Proptroller-XXXX** (the last four characters differ per board). The board
   always makes this network, also once it is on your own Wi-Fi.
   The password is **password**. It is a simple default, so anyone near the board who knows it can join: to change
   it, open **Settings → Wi-Fi → Setup hotspot password** on the board's page (step 2), type 8 to 63 characters,
   **Save settings** and **Restart**.
2. Open **http://192.168.4.1/** in the phone's browser. The **Live** tab shows every input and output: press your
   trigger and watch TRIG1 light up; tap OUT1's test button to check the lamp.
3. Optional: **Settings → Wi-Fi** puts the board on your own network: type its name or tap ▾ to pick one of the
   networks the board can see, then its password, **Save settings** and **Restart**. The board's own network stays on. On
   your own Wi-Fi open **http://proptroller.local/** (the board's Name as an address: rename it under Settings → Device
   and it follows after a restart). If another board already answers to that name, this one adds the end of its id,
   for example http://proptroller-ac2c.local/; the top of the Live tab (on the board's own network) shows the board's real address on your Wi-Fi.
   Some Android phones cannot open `.local` names: use the address your router gives the board (Specter Studio
   finds it for you). If it cannot join, the setup network comes back.

## 6. Your first effect script

Open the **Script** tab, delete what is in the box, and type (or paste):

```
effect porch light:
  wait for trig1
  out1 = on
  audio_single = 1     # plays sound 1 (the file in folder 01)
  wait 10
  fade out1 to 0% over 2
  wait 5               # a rest before it can run again
```

Press **Check**: the board says what is wrong in plain words, with the line number. Press **Save**, then press
your trigger. The light comes on, the sound plays, and after 10 s the light fades out.

That is the whole idea: each effect waits for its input, runs its lines from top to bottom, then waits again.
There are ten kinds of line; [firmware/SHOW-SCRIPT.md](firmware/SHOW-SCRIPT.md) explains them all, and
[firmware/show-script-examples/](firmware/show-script-examples/) has ready-made scripts with wiring notes (a
light-up ghost, doll eyes, a flickering lamp, a store prop's button on a relay, fog over DMX, a 12 V animatronic,
a doorbell chime).

## 7. If something goes wrong

- **Nothing happens when I press the trigger.** Watch TRIG1 on the Live tab. Not lighting: check the plug (tip and
  sleeve) or, for a powered sensor, the input type setting. Lighting but no effect: did you press Save?
- **No sound.** Check the card (FAT32, folders `01` … with `001…mp3` inside), the speaker wires, and that the
  script plays the right number.
- **A Pico W that keeps restarting.** Copy the first-flash file again (BOOTSEL). It keeps your settings.
- **Start over.** Settings → Factory reset on the board's page erases settings, Wi-Fi, shows and the script.

## Next steps

- Write effects in code: [firmware/arduino/lib/proptroller/README.md](firmware/arduino/lib/proptroller/README.md).
- Change the firmware itself: the developer quick start in the [README](README.md#developer-quick-start-vs-code).
- Build the firmware and flash it by hand, make a release folder, update over Wi-Fi:
  [docs/FIRMWARE-BUILD.md](docs/FIRMWARE-BUILD.md).
- The desktop app, Specter Studio (in [studio/](studio/README.md), installers on the
  [releases page](https://github.com/speck0413/specter-proptroller/releases)): it finds your boards, copies sounds
  to the card and edits scripts with blocks. [docs/DEV-SETUP.md](docs/DEV-SETUP.md) goes from a computer with nothing installed
  to Studio built from source and a board running the stock firmware.
