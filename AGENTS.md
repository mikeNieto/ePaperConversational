# AGENTS.md

## Project

ESP32-S3 firmware for Waveshare ESP32-S3-Touch-ePaper-1.54 voice conversational device.
Written in C/C++ targeting Arduino IDE (no PlatformIO config, no Makefile).

## Build

- Entry point: `ePaperConversational.ino` — Arduino IDE compiles this + all `.cpp`/`.h` files in project root and `src/`
- No CI, no lint, no typecheck, no tests
- The only verification is: compile with Arduino IDE and flash to hardware

## Architecture

- **Single core (Core 1)**: all FreeRTOS tasks (`LVGL`, `button_task`, `touch_task`, `state_task`, `wifi_task`, `bat_task`, `sleep_timer`, `transcription_task`, `chat_task`, `audio_task`) run on Core 1
- **State machine**: `AppState` enum (DEEP_SLEEP→ACTIVE→RECORD→LISTENING→SENDING→CONFIRM→WAITING→RESPONSE→DISCARDED), driven by `state_queue` (FreeRTOS queue of `AppEvent`) processed in `state_task`
- **LVGL v8.4**: widget-based UI, 200x200 e-paper display, full refresh always (`full_refresh = 1`)
  - **Mandatory**: lock LVGL mutex (`lvgl_lock(-1)` / `lvgl_unlock()`) before any widget operation from any task
  - Create screens via `lv_obj_create(NULL)`, load with `lv_scr_load()`, destroy old with `lv_obj_del()`
  - Flush callback converts RGB565 → 1-bit (threshold `0x7FFF`)
  - 2 display buffers allocated in SPIRAM, each `200*200*2 = 80000` bytes
- **Deep sleep**: `RTC_DATA_ATTR` variables persist (`boot_count`, `sleep_counter`, `conversation_uuid`, `uuid_is_null`). Wake causes: `EXT1` (user button) or `TIMER` (60 min auto-wake)
- **PSRAM critical**: all large allocations must use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` — LVGL buffers (80KB×2), WAV recording buffer (~2MB), MP3 download buffer (~200KB)

## Key conventions

- 2 buttons: **BOOT** (GPIO 0) controls selection/toggle, **PWR** (GPIO 18) usually acts as back/cancel
- Button event groups: `boot_groups`, `pwr_groups` — use `BTN_GET(ev, BIT)` macro
- Audio flow: microphone → ES8311 ADC → I2S → PSRAM buffer → WAV header → HTTP POST transcribe → confirm → chat → download MP3 → minimp3 decode → I2S play
- Partial refresh (`EPD_DisplayPart()`) for normal operation; full refresh (`EPD_Display()`) only before deep sleep
- Touch detection: runtime-probed via I2C at `0x38` (FT6336), flag `hasTouch` set accordingly
- Inactivity timer (60s) pauses during states LISTENING, SENDING, WAITING, RESPONSE
- All display strings use `currentLang->` (i18n struct `LangMessages`, currently only `MSG_ES`)

## Secrets warning

`user_config.h` contains hardcoded WiFi credentials, API base URL, and user ID. Never commit changes that expose real credentials.

## Directory map

| Directory | Purpose |
|-----------|---------|
| `src/display/` | E-paper SPI driver (`epaper_driver_display`) |
| `src/power/` | GPIO power control for EPD, audio, VBAT |
| `src/button_bsp/` | Multi-button press detection (`multi_button` lib) |
| `src/touch_bsp/` | FT6336 touch controller driver |
| `src/i2c_bsp/` | I2C bus initialization (SDA=47, SCL=48) |
| `src/battery/` | ADC battery voltage measurement |
| `src/codec_board/` | ES8311 codec init (wraps `esp_codec_dev`) |
| `src/esp_codec_dev/` | External codec library |
| `src/ui/screens.cpp` | All 8+ screen creation functions |
| `src/ui/status_bar.cpp` | WiFi + battery status bar widget |
| `specs/` | TechnicalSpec.md and ImplementationPlan.md (design docs) |
