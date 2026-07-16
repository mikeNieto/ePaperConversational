# AGENTS.md

## Project

ESP32-S3 firmware for Waveshare ESP32-S3-Touch-ePaper-1.54 voice conversational device.
Written in C/C++ targeting Arduino IDE (no PlatformIO config, no Makefile).
Uses ArduinoWebsockets library (`#include <ArduinoWebsockets.h>`), LVGL v8.4, minimp3.

## Build

- Entry point: `ePaperConversational.ino` — Arduino IDE compiles this + all `.cpp`/`.h`/`.c` files in project root and `src/`
- No CI, no lint, no typecheck, no tests
- Only verification: compile with Arduino IDE and flash to hardware
- **PSRAM must be enabled**: `heap_caps_malloc_extmem_enable(256)` called first in `setup()`
- **Secrets setup**: `cp user_config_secrets.example.h user_config_secrets.h` then edit with real WiFi/api credentials

## Architecture

- **Single core (Core 1)**: all FreeRTOS tasks pinned to Core 1 via `xTaskCreatePinnedToCore(..., 1)`
- **Task list** (name, priority, stack):
  - `LVGL` (4, 8KB) — LVGL tick handler, render loop, owns the LVGL mutex
  - `ws_task` (3, 20KB) — WebSocket connect/poll/send/receive, JSON parsing, audio buffer mgmt (needs 20KB due to String URL parsing + large local vars)
  - `state_task` (3, 8KB) — state machine consuming `state_queue` events, drives screen transitions
  - `button_task` (3, 4KB) — polls `boot_groups`/`pwr_groups` event bits, sends `AppEvent` to `state_queue`
  - `touch_task` (3, 4KB) — **only created if FT6336 detected**; sends `AppEvent` to `state_queue`
  - `wifi_task` (2, 4KB) — WiFi connect/reconnect loop
  - `bat_task` (1, 4KB) — periodic battery voltage read + status bar update
  - `sleep_timer` (1, 4KB) — inactivity timer (60s), skips during LISTENING/RECEIVING/RESPONSE(when playing)
- **State machine**: `AppState` enum with 5 states (`STATE_CONNECTING=0, STATE_RECORD=1, STATE_LISTENING=2, STATE_RECEIVING=3, STATE_RESPONSE=4`), driven by `AppEvent` struct (`type` + `data`) on `state_queue`
- **Event types**: `EVT_WS_CONNECTED`(1) `EVT_WS_DISCONNECTED`(2) `EVT_WS_ERROR`(3) `EVT_START_RECORDING`(4) `EVT_STOP_RECORDING`(5) `EVT_RECORDING_DONE`(6) `EVT_RESPONSE_READY`(7) `EVT_NEXT_MESSAGE`(8) `EVT_WS_RECONNECT`(9) `EVT_DISCARD`(10)
- **LVGL v8.4**: widget-based UI, 200x200 e-paper display, `full_refresh = 1` always
  - **Mandatory**: lock LVGL mutex (`lvgl_lock(-1)` / `lvgl_unlock()`) before any widget operation from any task
  - Screens: `lv_obj_create(NULL)` → build widgets → `lv_scr_load()` → `lv_timer_handler()` → `lv_obj_del(old_scr)`
  - Flush callback: `EPD_Clear()` → loop pixels → threshold RGB565 at `0x7FFF` → `EPD_DrawColorPixel()` → `EPD_DisplayPart()`
  - 2 display buffers (80KB each) in SPIRAM, fallback to internal RAM if SPIRAM fails
  - Touch: `lv_indev_touch_read_cb` reads from global `last_touch_x/y/pressed` set by `touch_task`
  - `lv_tick_inc(5)` driven by `esp_timer` at 5ms period
- **WebSocket communication** (not REST): all audio/data flows through a single WebSocket to `ws://<host>:<port>/ws`
  - Binary messages = MP3 or PCM audio from backend; text messages = JSON status/token/text/done/error and streaming control (`audio_start`/`audio_end` for PCM chunks)
  - Client sends: binary WAV audio + `{"type":"audio_end"}` text message
  - Thread-safe via `ws_cmd_queue` (FreeRTOS queue of `WsCmd` struct)
  - **No ArduinoJson** — JSON parsed with hand-rolled `parse_json_string()` via `String::indexOf` (requires Arduino's String class)
  - `WiFi.setSleep(false)` in `ws_task` to keep radio alive
- **Deep sleep**: `RTC_DATA_ATTR` vars: `boot_count`, `sleep_counter` (no UUID persistence in current code)
  - Wake causes: `EXT1` (GPIO0 or GPIO18, `ANY_LOW`) or `TIMER` (60 min)
  - Two entry paths: `enter_deep_sleep()` (full EPD refresh + `EPD_Display()`) and `enter_deep_sleep_light()` (no display refresh, used for timer auto-wake)
  - `rtc_gpio_hold_en(GPIO_NUM_17)` to keep VBAT powered
- **PSRAM critical**: large allocations use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` — LVGL buffers (80KB×2), recording buffer (1.92MB), MP3 download buffer (~200KB)

## Key conventions

- 2 buttons: **BOOT** (GPIO 0) = primary action, **PWR** (GPIO 18) = secondary/cancel
- Button events via `EventGroupHandle_t` (`boot_groups`, `pwr_groups`) — use `BTN_GET(ev, BIT)` with `BOOT_BIT_SINGLE/LONG/UP/DOUBLE` and `PWR_BIT_SINGLE/LONG/UP/DOUBLE`
  - **Watch out**: PWR bits have different order than BOOT (`PWR_BIT_DOUBLE=2` vs `BOOT_BIT_UP=2`, `PWR_BIT_UP=3` vs `BOOT_BIT_DOUBLE=3`)
- Button behavior per state:
  - `STATE_RECORD`: BOOT single → `EVT_START_RECORDING`; PWR single → deep sleep (immediate, not queue)
  - `STATE_LISTENING`: BOOT single → `EVT_STOP_RECORDING`; PWR single → `EVT_DISCARD`
  - `STATE_RESPONSE`: BOOT single → `EVT_NEXT_MESSAGE` (continues conversation); PWR single → `EVT_WS_RECONNECT`
  - `STATE_CONNECTING` and `STATE_RECEIVING`: buttons have no effect
- Touch behavior per state: `STATE_RECORD` → sends `EVT_START_RECORDING`; `STATE_LISTENING` → sends `EVT_STOP_RECORDING`
- Audio recording: 16kHz 16-bit stereo, max 30 seconds (~1.92MB PSRAM buffer). Auto-discards on timeout (`EVT_RECORDING_DONE` with empty buffer → back to RECORD). Recording runs in a FreeRTOS task (`rec_task_handle`).
- Partial refresh (`EPD_DisplayPart()`) for normal operation; full refresh (`EPD_Display()` after `EPD_Init()`) only in `enter_deep_sleep()` before deep sleep
- Touch detection: runtime-probed via I2C at `0x38` (FT6336), flag `hasTouch` set; `touch_task` only created if `hasTouch == true`
- Inactivity timer (60s): pauses if `STATE_LISTENING`, `STATE_RECEIVING`, or `STATE_RESPONSE` with audio playing (`audio_wav_is_playing()`)
- All display strings use `currentLang->` (i18n struct `LangMessages`, only `MSG_ES` implemented)
- `lvgl_lock()` returns `bool` — check the return value; if false, don't proceed with widget operations

## Config and secrets

Configuration is split into two files:

- **`user_config.h`** — tracked in git. Contains all non-secret config: GPIO pins, timing constants, SPI/I2C settings, display params, deep sleep settings. **When adding new config constants, add them here.**
- **`user_config_secrets.h`** — gitignored (not tracked). Contains ONLY the 3 secret defines: `WIFI_SSID`, `WIFI_PASSWORD`, `API_BASE_URL`. `user_config.h` includes this file at the end.
- **`user_config_secrets.example.h`** — tracked template with dummy values. New clones: `cp user_config_secrets.example.h user_config_secrets.h` and edit with real values.

**Rule**: if a new constant is NOT a secret (no passwords, keys, personal IPs/URLs), put it in `user_config.h` only. If it IS a secret, add it to BOTH `user_config_secrets.h` and `user_config_secrets.example.h` (with a dummy in the example).

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
| `src/esp_codec_dev/` | External codec library (v1.3.5, ESP-IDF component) |
| `src/ui/screens.cpp` | 6 screen creation functions + receiving status updater |
| `src/ui/status_bar.cpp` | WiFi + battery status bar widget |
| `specs/` | TechnicalSpec.md and ImplementationPlan.md (design docs — **note: architecture has diverged**; spec describes REST API but code uses WebSocket) |

## State transition diagram (actual code)

```
CONNECTING ──[WS connected]──▶ RECORD ──[start recording]──▶ LISTENING
    ▲                              ▲                              │
    │                              │                     [stop/done + valid WAV]
    │  [WS disconnect from         │                              ▼
    │   RECORD/LISTENING/          │◀──[empty WAV]─── RECEIVING ──[response ready]──▶ RESPONSE
    │   RECEIVING/RESPONSE]        │                                  │
    └──────────────────────────────┘      [WS error] ────────────────┘
                                         [WS disconnect]

RESPONSE ──[next message]──▶ LISTENING (continue conversation)
RESPONSE ──[reconnect]───▶ CONNECTING
``` |
