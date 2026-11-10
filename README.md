# Larp Charm

A bag charm with a screen, built on the Waveshare ESP32-S3-Touch-LCD-1.69. This repo holds the web app that controls it over Bluetooth and the firmware that runs on it.

- `index.html`, `app.js`, `charm.js`: the web app (GitHub Pages). Open it in Chrome on Android or a laptop and press Connect.
- `firmware/LarpCharm/LarpCharm.ino`: firmware 3.0 for the Arduino IDE.

## What it does
- Screens: Clock, Text, Photo, Clips (GIFs and videos), Music, Pet, Toys, Card
- Clock: nine animated backgrounds (some follow gravity) and four clock styles
- Motion: shake to wake, shake to shuffle backgrounds, roll dice or ask the 8-ball, pet leans when tilted
- Timer, hourly chime, sound effects, charging indicator
- Spotify now-playing, NFC tag writer

## Flashing
Board: ESP32S3 Dev Module. USB CDC On Boot: Enabled. Flash: 16MB. PSRAM: OPI.
Partition scheme: 16M Flash (3MB APP/9.9MB FATFS).
Libraries: GFX Library for Arduino, NimBLE-Arduino, JPEGDEC.

The web app and firmware versions go together: this app needs firmware 3.0.
