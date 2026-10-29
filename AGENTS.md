# CrossPoint Plus: agent guide

Personal X4 Pro firmware forked from CrossPoint Reader. Read [README.md](README.md) and
[docs/plus/ROADMAP.md](docs/plus/ROADMAP.md) (decision log) before changing behavior.

## Workflow

- Work happens on `main` of `lidiaev/crosspoint-plus`; pushing there is allowed. Pull requests come later.
- Version lives in `platformio.ini` (`1.6.5-plus.N`); bump it for each firmware handed to the device.
- Build: `~/.platformio/penv313/bin/pio run -e x4pro-gh_release` (Python 3.13 venv).
- Format: `PATH=~/.platformio/penv313/bin:$PATH ./bin/clang-format-fix` — clang-format 21 (`pip install clang-format==21.1.8` in the venv), the version CI checks with; never call clang-format directly.
- Static analysis: `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, as CI runs it.
- Host tests: `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test`.
- Device upload over Wi-Fi when File transfer is on: `curl -F file=@firmware.bin "http://<ip>/upload?path=/&overwrite=1"`,
  always as `crosspoint-plus-x4pro.bin` (one firmware file on the card). Never delete files on the device.
- The FreeInk SDK is a submodule; local changes to it live in `scripts/freeink_sdk_patches/` and are applied by
  `scripts/patch_freeink_sdk.py` at build time.

## Hardware

ESP32-S3, 8 MB PSRAM, 480×800 UC8279 e-ink panel, GT911 multi-touch, frontlight, Home key, side buttons, no front
buttons. Fast typing uses a custom LUT and windowed partial updates (see `src/components/typing/FastTyping.h` and
docs/plus/TYPING.md).

## Code rules

- Use the HAL (`HalDisplay`, `HalGPIO`, `HalStorage`/`Storage`), never SdFat or SDK classes directly. All SD access
  goes through `HalStorage`.
- User-facing text goes through `tr()` with keys in `lib/I18n/translations/*.yaml` (English and Russian at least).
- No bare `new`: use `makeUniqueNoThrow` (`lib/Memory/Memory.h`); `-fno-exceptions` makes failed `new` abort.
- Activities: `loop()` runs on the input task, `render()` on the render task under `RenderLock`. `onEnter`/`onExit`
  are called by `ActivityManager` with the render lock held: never take `RenderLock` inside `onExit`.
- Shared input→render state goes through a queue or atomics (see `NoteEditorActivity`).
- Keep comments short and about the current code; rationale goes into commit messages.
- Match the surrounding code; avoid unrelated refactors.
