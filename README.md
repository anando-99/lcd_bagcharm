# Larp Charm

A bag charm with a screen, built on the Waveshare ESP32-S3-Touch-LCD-1.69. This repo holds the web app that controls it over Bluetooth and the firmware that runs on it.

- `index.html`, `app.js`, `charm.js`: the web app (GitHub Pages). Open it in Chrome on Android or a laptop and press Connect.
- `firmware/LarpCharm/LarpCharm.ino`: firmware 4.0 for the Arduino IDE.

## What it does
- One fused screen, the deck: time, the song that is playing, the pet, the motion sensor and your message, all live. Tap a block to open it full screen, swipe down to close it.
- Blocks: Chrono, Audio, Daemon (pet), RNG (oracle, dice, coin), Msg, Img, Vid (GIFs and videos), ID card
- Audio: the album cover fills the whole screen; tap left, middle or right to go back, pause or skip (Spotify Premium)
- Six colour themes, eleven moving backdrops, four clock styles; the deck takes the album's colour while a song plays
- Motion: shake to wake, shake to shuffle backdrops, roll dice or ask the oracle, pet leans when tilted
- Timer, hourly chime, sound effects, charging indicator, NFC tag writer

## Flashing
Board: ESP32S3 Dev Module. USB CDC On Boot: Enabled. Flash: 16MB. PSRAM: OPI.
Partition scheme: 16M Flash (3MB APP/9.9MB FATFS).
Libraries: GFX Library for Arduino, NimBLE-Arduino, JPEGDEC.

The web app and firmware versions go together: this app needs firmware 4.0.
