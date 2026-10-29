# CrossPoint Plus

Personal firmware for the **Xteink X4 Pro**, forked from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
It keeps CrossPoint's reader and library and adds a fast Markdown **Notes** app with its own touch keyboard,
which is also the system keyboard.

- Device: X4 Pro only (ESP32-S3, 8 MB PSRAM, GT911 touch, UC8279 panel). Use upstream CrossPoint for anything else.
- Versions: `<upstream version>-plus.<build>`, for example `1.6.5-plus.23`. The built-in OTA updater checks this
  repository's [Releases](https://github.com/lidiaev/crosspoint-plus/releases).
- Plan, decisions and design notes (in Russian): [docs/plus](docs/plus).

## What differs from CrossPoint

- **Notes**: `/notes/*.md` with Markdown rendered in place (headings, bold, italic, strikethrough, code, lists,
  tasks with tap-to-check, quotes, rules), a format toolbar above the keyboard, search, pin and delete. Keys reach the
  e-ink panel in about 300 ms.
- **Keyboard**: one iPhone-style RU/EN keyboard for notes and every text field, with a contextual Return key and a
  cursor you can place by tapping.
- **Simpler system**: one theme (Lyra), Russian and English only, one built-in font (Ubuntu); books use the font you
  install from the web Fonts page. The Library is a single Recent list; the quick panel holds the light sliders, night
  mode and orientation.
- **Removed**: the file browser on the home screen, OPDS, KOReader sync, Calibre Connect, USB drive mode, the font
  downloader and the extra themes.

How the code is organised: [docs/plus/ARCHITECTURE.md](docs/plus/ARCHITECTURE.md).

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
