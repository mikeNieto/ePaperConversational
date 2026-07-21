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
  - `ws_task` (3, 20KB) — WebSocket connect/poll/send/receive, JSON parsing, audio buffer mgmt (needs 20KB due to String URL parsing + large local vars). Also writes incoming PCM chunks to the streaming ring buffer via `stream_buf_write()`.
  - `stream_task` (5, 8KB) — **created dynamically on `audio_start`**; streaming audio playback: reads from ring buffer, writes to ES8311 codec, closes on `audio_end` or timeout
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
  - Binary messages = PCM audio chunks from backend (streaming); text messages = JSON status/token/text/done/error and streaming control (`audio_start`/`audio_end`)
  - Client sends: binary WAV audio + `{"type":"audio_end"}` text message
  - **Streaming audio protocol** (backend → client):
    - `{"type":"audio_start","sample_rate":24000,"channels":1,"bits":16}` — begins a PCM stream. Client creates a 512KB PSRAM ring buffer and spawns `stream_playback_task` (priority 5)
    - Binary PCM chunks (typically 32KB each) — client writes to ring buffer via `stream_buf_write()`. Blocks with 5s timeout if buffer is full (backpressure via TCP window)
    - `{"type":"audio_end"}` — signals end of stream. Client calls `stream_buf_signal_end()`. Playback task drains remaining data and closes codec
    - `{"type":"done"}` — triggers `EVT_RESPONSE_READY`, transitions UI to `STATE_RESPONSE` (audio may already be playing or finished)
    - **Backend must send chunks at real-time speed** (~48KB/s for 24000Hz mono 16-bit), interleaving text JSON messages between binary chunks. Burst sends will cause TCP backpressure blocking
  - Thread-safe via `ws_cmd_queue` (FreeRTOS queue of `WsCmd` struct)
  - **No ArduinoJson** — JSON parsed with hand-rolled `parse_json_string()` via `String::indexOf` (requires Arduino's String class)
  - `WiFi.setSleep(false)` in `ws_task` to keep radio alive

- **Streaming audio playback** (`audio_stream.h`/`.cpp`, `audio_bsp.cpp:stream_playback_task`):
  - **Ring buffer** (`audio_stream.cpp`): 512KB circular buffer in PSRAM (`STREAM_BUF_SIZE`), single-producer (WS callback) / single-consumer (playback task)
  - **Blocking writes**: `stream_buf_write(data, len, timeout_ms)` blocks up to `timeout_ms` if buffer is full, using a binary semaphore signaled by the reader. Provides natural TCP backpressure
  - **Playback task** (`stream_playback_task`, priority 5): spawned by `audio_stream_playback_start()` when `audio_start` JSON arrives
    - Waits for min fill threshold (`STREAM_MIN_FILL_BYTES` = 24KB, ~0.5s) or `audio_end` signal
    - Opens ES8311 codec at the specified sample rate/channels/bits
    - Reads 4KB chunks from ring buffer, writes to codec, yields 1ms between successful writes (gives `ws_task` priority 3 time to run)
    - On `stream_buf_is_ended()` + empty buffer: closes codec, frees ring buffer, sets `wav_playing = false`
    - On `wav_stop_flag`: aborts immediately (triggered by `audio_play_wav_stop()`)
    - Timeout: aborts if no data for `STREAM_TIMEOUT_MS` (10s) from last successful read
  - **Fallback path**: if backend sends a single binary WAV/PCM without `audio_start`/`audio_end` protocol, `switch_state(STATE_RESPONSE)` checks `!audio_wav_is_playing()` and falls back to `audio_play_wav_start()`/`audio_play_pcm_start()` with the old full-buffer approach
  - **Cleanup on error/disconnect**: `STATE_RECEIVING` handlers call `audio_play_wav_stop()` + `ws_free_audio_buffer()` (frees ring buffer via `stream_buf_free()`) before transitioning
  - **Config constants** (`user_config.h`): `STREAM_BUF_SIZE` (524288), `STREAM_MIN_FILL_BYTES` (24000), `STREAM_TIMEOUT_MS` (10000)

- **Deep sleep**: `RTC_DATA_ATTR` vars: `boot_count`, `sleep_counter` (no UUID persistence in current code)
  - Wake causes: `EXT1` (GPIO0 or GPIO18, `ANY_LOW`) or `TIMER` (60 min)
  - Two entry paths: `enter_deep_sleep()` (full EPD refresh + `EPD_Display()`) and `enter_deep_sleep_light()` (no display refresh, used for timer auto-wake)
  - `rtc_gpio_hold_en(GPIO_NUM_17)` to keep VBAT powered
- **PSRAM critical**: large allocations use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` — LVGL buffers (80KB×2), recording buffer (1.92MB), streaming ring buffer (512KB)

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
- **LED (GPIO 3) is active-low**: `LOW` (0) = ON, `HIGH` (1) = OFF. Don't change the polarity logic in `wifi_bsp.cpp:led_set()` — `wifi_led_write(true)` means "LED on" (writes 0) and `wifi_led_write(false)` means "LED off" (writes 1).
- **LED control**: managed by `ws_task` (blinks at 200ms while `STATE_CONNECTING`) and `switch_state()` (turns off when leaving `STATE_CONNECTING`). `wifi_task` no longer touches the LED.
- **Beep notification sounds**: short audio chirps played to indicate state transitions:
  - `AUDIO_BEEP_START` (800 Hz): plays when entering `STATE_LISTENING` (recording begins). 200ms total (70ms silence + 130ms tone) at 24000Hz mono 16-bit.
  - `AUDIO_BEEP_STOP` (500→800 Hz ascending two-tone): plays after recording stops, before sending audio to WebSocket. 200ms total (50ms silence + 75ms@500Hz + 75ms@800Hz) at 24000Hz mono 16-bit, half volume (amplitude 4000).
  - `AUDIO_BEEP_DISCARD` (300 Hz): plays when recording is discarded via PWR button (`EVT_DISCARD`). 400ms total (70ms silence + 330ms tone) at 24000Hz mono 16-bit.
  - `AUDIO_BEEP_RECONNECT` (1000 Hz): plays when PWR is pressed during `STATE_RESPONSE` to reconnect WebSocket (`EVT_WS_RECONNECT`). 400ms total (70ms silence + 330ms tone) at 24000Hz mono 16-bit.
  - `AUDIO_BEEP_SLEEP` (900→600 Hz descending two-tone): plays when entering deep sleep (`enter_deep_sleep()`). 250ms total (50ms silence + 100ms@900Hz + 100ms@600Hz) at 24000Hz mono 16-bit, half volume (amplitude 4000).
  - `AUDIO_BEEP_WAKE` (600→900 Hz ascending two-tone): plays on wake from deep sleep via button (`ESP_SLEEP_WAKEUP_EXT1`) or other non-timer wake. 250ms total (50ms silence + 100ms@600Hz + 100ms@900Hz) at 24000Hz mono 16-bit, half volume (amplitude 4000). Similar to Windows device connect/disconnect sounds.
  - All use `audio_beep_play_standalone()` (`audio_bsp.cpp`) which generates a WAV in heap and plays it via `audio_play_wav_start()` → `wav_playback_task`. This is the **same codec path as the response audio**, ensuring reliable I2S TX DMA initialization.
  - **Silence padding is critical**: the I2S TX DMA needs ~50-70ms of priming data before audio becomes audible. Without leading silence, short beeps (<200ms) are completely inaudible because the DMA never starts transmitting before the codec is closed.
  - `audio_beep_play()` (`audio_bsp.cpp`): alternative direct-write variant (writes PCM via `esp_codec_dev_write()` to an already-open codec). Only works when codec is freshly opened — **do not use after close/reopen cycles** as the ES8311 DAC path may not re-enable.
  - Beep integration points in `user_app.cpp`:
    - Start beep: `switch_state(STATE_LISTENING)` → `audio_beep_play_standalone(AUDIO_BEEP_START)` → `audio_play_init()` → `audio_start_recording()`
    - Stop beep: `EVT_STOP_RECORDING` handler → `audio_stop_recording()` → `audio_beep_play_standalone(AUDIO_BEEP_STOP)` → send WAV
    - Discard beep: `EVT_DISCARD` handler → `audio_stop_recording_no_close()` → `audio_beep_play_standalone(AUDIO_BEEP_DISCARD)` → `audio_discard_recording()` → `STATE_RECORD`
    - Reconnect beep: `EVT_WS_RECONNECT` handler → `audio_play_wav_stop()` → `ws_free_audio_buffer()` → `audio_beep_play_standalone(AUDIO_BEEP_RECONNECT)` → `ws_request_reconnect()` → `STATE_CONNECTING`
    - Sleep beep: `enter_deep_sleep()` → `audio_beep_play_standalone(AUDIO_BEEP_SLEEP)` → display + deep sleep
    - Wake beep: `setup()` (EXT1/other wake) sets `g_play_wake_beep = true` → `state_task` on `EVT_WS_CONNECTED` → `audio_beep_play_standalone(AUDIO_BEEP_WAKE)` → `g_play_wake_beep = false` → `switch_state(STATE_RECORD)`. Only plays on wake-from-sleep, not on first boot or WS reconnects.
  - Helper functions: `audio_stop_recording_no_close()` (stops recording task, keeps codec open), `audio_close_codec()` (closes both playback+record handles).

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
| `audio_stream.h` / `audio_stream.cpp` | Streaming ring buffer: 512KB PSRAM circular buffer with blocking writes + binary semaphore backpressure |
| `ws_client.h` / `ws_client.cpp` | WebSocket client: connect, send/receive, JSON parsing, audio buffer mgmt, streaming protocol handling |
| `audio_bsp.h` / `audio_bsp.cpp` | ES8311 codec control, recording task, WAV/PCM playback tasks, streaming playback init |
| `user_config.h` | All non-secret configuration constants including streaming params |
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
