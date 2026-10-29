# CrossPoint Plus

Personal firmware for the **Xteink X4 Pro**, forked from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
It keeps CrossPoint's reader and library and adds a fast Markdown **Notes** app with its own touch keyboard,
which is also the system keyboard.

- Device: X4 Pro only (ESP32-S3, 8 MB PSRAM, GT911 touch, UC8279 panel). Use upstream CrossPoint for anything else.
- Versions: `<upstream version>-plus.<build>`, for example `1.6.5-plus.15`. The built-in OTA updater checks this
  repository's [Releases](https://github.com/lidiaev/crosspoint-plus/releases).
- Plan, decisions and design notes (in Russian): [docs/plus](docs/plus).

## What differs from CrossPoint

- **Notes**: `/notes/*.md`, Markdown rendered in place, fast typing (~300 ms per key on e-ink), pin and delete,
  search.
- **Keyboard**: one iPhone-style RU/EN keyboard for notes and every text field.
- **Removed**: the file browser on the home screen, OPDS, KOReader sync, Calibre Connect, USB drive mode, the font
  downloader, keyboard layout picker and all themes except Lyra.

## Build

```bash
~/.platformio/penv313/bin/pio run -e x4pro-gh_release
```

PlatformIO needs Python 3.13 (the ESP32 platform rejects newer versions). The firmware is at
`.pio/build/x4pro-gh_release/firmware.bin`; flash it from the SD card (Settings → System → firmware update from SD)
or over USB with `pio run -e x4pro-gh_release -t upload`.

Host unit tests:

```bash
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test
```

## Release

Push a tag matching `*-plus.*` (for example `1.6.5-plus.16`); the release workflow builds and publishes the binary.

## License

MIT, as upstream. See [LICENSE](LICENSE).
