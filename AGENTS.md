# AGENTS.md

## Project

ESP32-S3 firmware for the Waveshare ESP32-S3-Touch-ePaper-1.54 voice conversational device.
C/C++ for the **Arduino IDE** — there is no PlatformIO config, no Makefile, no CMake for the sketch.

## Build and verification

- Sketch entry: `ePaperConversational.ino`. Arduino IDE compiles it plus every `.c`/`.cpp` in the project root and `src/`. **Any file you add to those directories is compiled**, even if nothing includes it.
- **No CI, no lint, no typecheck, no tests.** The only verification is "compiles in Arduino IDE + flashes and runs on hardware". Do not claim a change is verified without that.
- Required Arduino libraries (only two external): `lvgl` **9.x** (exactly one copy installed; use the tracked `lv_conf.h` as its config in the Arduino sketchbook) and `ArduinoWebsockets`. `multi_button`, `esp_codec_dev` and `codec_board` are vendored under `src/`.
- **PSRAM is mandatory**: select `OPI PSRAM` for the board. `ePaperConversational.ino:7` `#error`s if `BOARD_HAS_PSRAM` is not defined. `setup()` calls `heap_caps_malloc_extmem_enable(256)` first.
- 8MB flash. Image is ~1.25MB against the default 1.31MB app partition — nearly full. If you add features, expect to switch to an 8MB / `Huge APP` partition scheme. `lv_conf.h` deliberately disables unused LVGL widgets/themes/layouts/decoders/drivers/demos to stay under the limit; don't re-enable them casually.
- **Secrets**: `cp user_config_secrets.example.h user_config_secrets.h`, then fill in real values. The real file is gitignored; the build fails without it (`user_config.h:76`).

## Non-obvious gotchas

- **`user_config.h` must be included before `<ArduinoWebsockets.h>`** (see `ws_client.cpp:3-4`). It defines `_WS_CONFIG_MAX_MESSAGE_SIZE` (512KB); reordering silently reverts to the library's small default and truncates PCM chunks.
- **Dead code, still compiled**: `api_client.cpp`/`api_client.h` (REST/HTTPClient) and `minimp3.h` are not included by any live file. The REST path was replaced by WebSocket. Don't extend them; they only survive because Arduino compiles the folder. `api_client.cpp` is the reason `HTTPClient` is still linked.
- **`API_BASE_URL` is an `http://host:port` URL** even though transport is WebSocket. `ws_client.cpp:368-377` parses host/port out of it and builds `ws://host:port/ws`.
- **`specs/TechnicalSpec.md` is stale** — it describes a REST API. The code uses WebSocket. Trust the code.
- **LED on GPIO 3 is active-low**: `LOW`=on, `HIGH`=off. `wifi_bsp.cpp:led_set()` already inverts; `wifi_led_write(true)` = LED on. Don't "fix" the polarity.
- **PWR button bit order differs from BOOT**: `PWR_BIT_DOUBLE=2`/`PWR_BIT_UP=3` vs `BOOT_BIT_UP=2`/`BOOT_BIT_DOUBLE=3`. Use the named constants with `BTN_GET(ev, BIT)`.
- **Beeps need leading silence**: I2S TX DMA needs ~50-70ms of priming samples or short beeps are completely inaudible. Every beep waveform starts with silence. Do not "optimize" it away.
- **Use `audio_beep_play_standalone()`**, not `audio_beep_play()`. The latter writes directly to an already-open codec and fails after a close/reopen cycle (ES8311 DAC path does not re-enable).
- **No ArduinoJson.** JSON is hand-parsed in `ws_client.cpp` (`parse_json_string` / `parse_json_int` / `parse_json_has_type`) using Arduino `String::indexOf`, with manual `\uXXXX` decoding. Adding ArduinoJson would blow the flash budget.
- Built-in Montserrat 14 is disabled in `lv_conf.h`. The project font is `font_montserrat_latin_14.c` (14px/4bpp, ASCII + Latin-1 `0xA1-0xFF` for `ñ`, accented vowels, `¿`, `¡`). New glyphs require regenerating that file.
- LVGL themes are disabled, so styled objects must set opacity explicitly (e.g. battery fill bars use `LV_OPA_COVER`).

## Architecture

All FreeRTOS tasks are pinned to **Core 1** (`xTaskCreatePinnedToCore(..., 1)`).

| Task | Prio | Stack | Notes |
|------|------|-------|-------|
| `LVGL` | 4 | 8KB | render loop; owns the LVGL mutex |
| `ws_task` | 3 | 20KB | WS connect/poll/send, JSON parsing; 20KB is needed for `String` URL parsing + large locals |
| `stream_task` | 5 | 8KB | created dynamically on `audio_start`, exits on `audio_end`/timeout |
| `state_task` | 3 | 8KB | consumes `state_queue`, drives screen transitions |
| `btn_task` | 3 | 4KB | polls `boot_groups`/`pwr_groups` event bits |
| `touch_task` | 3 | 4KB | **only created if FT6336 is detected at I2C `0x38`** (`hasTouch`) |
| `wifi_task` | 2 | 4KB | reconnect loop |
| `bat_task` | 1 | 4KB | battery read + status bar |
| `sleep_timer` | 1 | 4KB | 60s inactivity timer |

### LVGL

- **Always take the LVGL mutex** (`lvgl_lock(-1)` / `lvgl_unlock()`) before touching any widget from any task.
- Screen swap pattern: `create_screen_*()` → `lv_screen_load()` → `lv_timer_handler()` → `lv_obj_delete(old_scr)` (see `switch_state()` in `user_app.cpp`).
- `LV_DISPLAY_RENDER_MODE_FULL`, RGB565, 2 buffers of 80KB in SPIRAM (fallback to internal RAM). LVGL's 48KB pool is also SPIRAM via `LV_MEM_POOL_ALLOC` (needs `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`).
- Flush cb (`user_app.cpp:297`): `EPD_Clear()` → threshold each RGB565 pixel at `0x7fff` → `EPD_DrawColorPixel()` → `EPD_DisplayPart()` → `lv_display_flush_ready()`.
- Partial refresh everywhere; full refresh (`EPD_Init()` + `EPD_Display()`) only in `enter_deep_sleep()`.
- Touch feeds LVGL through a lock-free ring buffer (`touch_sample_push`/`touch_sample_pop`) and sets `data->continue_reading`, not just the `last_touch_*` globals (those are the fallback).
- `lv_tick_inc(5)` from an `esp_timer` at 5ms.

### Boot sequence (race-sensitive)

`lvgl_port_init()` and `user_ui_init()` are both idempotent (static guards) and are called **inside `user_app_init()` before any task is created** — the display driver must be registered before `state_task` can call `switch_state()`. `user_ui_init()` loads the "connecting" screen before the **blocking** `wifi_init()` so the screen isn't blank during the WiFi scan. `setup()` then calls `switch_state(g_app_state)` if the state already advanced past `STATE_CONNECTING`, to undo the race where `user_ui_init()` overwrote the correct screen.

### State machine

`AppState`: `STATE_CONNECTING=0, STATE_RECORD=1, STATE_LISTENING=2, STATE_RECEIVING=3, STATE_RESPONSE=4, STATE_SETTINGS=5` (`user_app.h`).
`AppEvent { uint8_t type; uint8_t data; }` on `state_queue`; event IDs are `#define`s 1..13 in `user_app.h:26-38`.

```
CONNECTING ──[EVT_WS_CONNECTED]──▶ RECORD ──[EVT_START_RECORDING]──▶ LISTENING
     ▲                               ▲  ▲                                │
     │                               │  └──[empty WAV]───┐    [stop + valid WAV]
     │                               │                   │               ▼
     │◀──[WS disconnect / error from any state]──────────┴────────── RECEIVING
     │                                                                   │
     │                                                        [EVT_RESPONSE_READY]
     │                                                                   ▼
     └──────────────[EVT_WS_RECONNECT]──────────────────────────────  RESPONSE
                                                                        │
RECORD ◀──[EVT_EXIT_SETTINGS]── SETTINGS ◀──[EVT_OPEN_SETTINGS]── RECORD│
SETTINGS ──[EVT_TOGGLE_LANGUAGE]──▶ SETTINGS (re-render)                │
LISTENING ◀──────────────────[EVT_NEXT_MESSAGE]─────────────────────────┘
```

Buttons — **BOOT** (GPIO 0) = primary, **PWR** (GPIO 18) = secondary/cancel:

| State | BOOT single | PWR single |
|-------|-------------|------------|
| `RECORD` | `EVT_START_RECORDING` | `EVT_OPEN_SETTINGS` |
| `LISTENING` | `EVT_STOP_RECORDING` | `EVT_DISCARD` |
| `RESPONSE` | `EVT_NEXT_MESSAGE` | `EVT_WS_RECONNECT` |
| `SETTINGS` | `EVT_TOGGLE_LANGUAGE` | `EVT_EXIT_SETTINGS` |
| `CONNECTING`, `RECEIVING` | ignored | ignored |

PWR **long** → deep sleep from any state (checked before the per-state switch). Touch: `RECORD` → `EVT_START_RECORDING`, `LISTENING` → `EVT_STOP_RECORDING`.

### WebSocket protocol

Single WS to `ws://<host>:<port>/ws`. Binary frames = PCM audio from backend; text frames = JSON.

Client → backend: binary WAV, then `{"type":"audio_end"}`. Commands are marshalled onto `ws_cmd_queue` (`WsCmd`) so other tasks never touch the socket. `WiFi.setSleep(false)` in `ws_task`.

Backend → client streaming sequence:
1. `{"type":"audio_start","sample_rate":24000,"channels":1,"bits":16}` — allocates the 512KB PSRAM ring buffer and spawns `stream_task`.
2. Binary PCM chunks (~32KB) → `stream_buf_write()`, which **blocks up to `STREAM_TIMEOUT_MS` when full** (deliberate TCP backpressure).
3. `{"type":"audio_end"}` → `stream_buf_signal_end()`; playback drains then closes the codec.
4. `{"type":"done"}` → `EVT_RESPONSE_READY` → `STATE_RESPONSE` (audio may still be playing).

**The backend must pace chunks at real time** (~48KB/s for 24kHz mono 16-bit) and interleave text JSON between binary frames; bursting stalls the socket.
Incoming `status`/`token` text goes through `queue_screen_receiving_status()`, which coalesces via an LVGL timer so UI redraws don't block PCM reception.

### Streaming audio playback

`audio_stream.{h,cpp}` (ring buffer) + `audio_bsp.cpp:stream_playback_task`.

- 512KB PSRAM circular buffer, single producer (`onMessageCallback`) / single consumer (playback task); blocking writes gated by a binary semaphore the reader signals.
- Playback waits for `STREAM_MIN_FILL_BYTES` (24KB, ~0.5s) or `audio_end`, opens ES8311 at the negotiated format, then writes 1KB frame-aligned chunks and retries transient I2S errors. The blocking I2S write is what yields to `ws_task` — do not add a fixed delay.
- Ends on `stream_buf_is_ended()` + empty buffer; aborts on `wav_stop_flag` (`audio_play_wav_stop()`) or `STREAM_TIMEOUT_MS` with no successful write.
- **Fallback**: a lone binary WAV/PCM with no `audio_start`/`audio_end` is handled in `switch_state(STATE_RESPONSE)` via `!audio_wav_is_playing()` → `audio_play_wav_start()`/`audio_play_pcm_start()` (old full-buffer path).
- Any error/disconnect handler in `STATE_RECEIVING` must call `audio_play_wav_stop()` **and** `ws_free_audio_buffer()` (which frees the ring buffer) before transitioning.

### Audio recording and beeps

- Recording: 16kHz 16-bit **stereo**, max 30s → ~1.92MB PSRAM buffer (`REC_BUFFER_SIZE`), runs in `rec_task`. Timeout emits `EVT_RECORDING_DONE` with an empty buffer → back to `RECORD`.
- Beep IDs in `audio_bsp.h`: `AUDIO_BEEP_START` / `STOP` / `DISCARD` / `RECONNECT` / `SLEEP` / `WAKE`. All generated in heap and played through `audio_play_wav_start()` → `wav_playback_task`, i.e. the same codec path as response audio (that is what makes I2S TX DMA init reliable).
- Wake beep: `setup()` sets `g_play_wake_beep = true` **before** `user_app_init()`; `state_task` consumes it on `EVT_WS_CONNECTED`. Setting it later races with the event.
- Helpers: `audio_stop_recording_no_close()` (stop task, keep codec open), `audio_close_codec()` (close playback + record handles).

### Power, WiFi, i18n

- Deep sleep: `RTC_DATA_ATTR` `boot_count`, `sleep_counter`, `g_lang_index`. Wake on `EXT1` (GPIO0 or GPIO18, `ANY_LOW`) or `TIMER` (`SLEEP_DURATION_SEC`, 3600s). `enter_deep_sleep()` does a full EPD refresh; `enter_deep_sleep_light()` skips it (timer auto-wake path). `rtc_gpio_hold_en(GPIO_NUM_17)` keeps VBAT powered.
- Inactivity timer (60s) pauses in `LISTENING`, `RECEIVING`, and `RESPONSE` while `audio_wav_is_playing()`. It does **not** pause in `SETTINGS`.
- Multi-WiFi: `WIFI_NETWORKS` / `WIFI_NETWORK_COUNT` in the secrets header. `wifi_connect_best()` scans, filters to known SSIDs, picks strongest RSSI. It early-returns if already `WL_CONNECTED` (prevents scan-induced WS drops) and **blocks** up to `WIFI_CONNECT_TIMEOUT_MS` (10s). `wifi_task` calls it on every disconnect; rescans every 5s if nothing known is in range.
- LED is driven only by `ws_task` (200ms blink while `STATE_CONNECTING`) and `switch_state()` (off when leaving `CONNECTING`). `wifi_task` must not touch it.
- i18n: `LangMessages` in `messages.h` — a `name` field plus 15 UI strings. Tables `MSG_ES`, `MSG_EN`; `currentLang` set by `lang_init()` (first call in `setup()`) from `g_lang_index`; `lang_toggle()` rotates `lang_table[]` (count derived via `sizeof`). To add a language: define `MSG_XX`, `extern` it, append to `lang_table[]`. All UI reads `currentLang->field`.
- Battery: ADC1_CH3 / GPIO4, 12dB, curve-fit calibration, 1:2 divider. `battery_get_status()` averages 16 samples 2ms apart, trims 2 at each extreme, maps through a LiPo curve (`4.12V`=100%, `3.30V`=0%). The label and the 4 fill bars (25% steps) share that one reading.

## Config and secrets

- **`user_config.h`** (tracked) — every non-secret constant: pins, timings, SPI/I2C, display, streaming (`STREAM_BUF_SIZE`, `STREAM_MIN_FILL_BYTES`, `STREAM_TIMEOUT_MS`), WS (`WS_REQUEST_TIMEOUT_MS`, `AGENT_TEXT_SIZE`, `_WS_CONFIG_MAX_MESSAGE_SIZE`), WiFi timeout, battery curve. **New non-secret constants go here and nowhere else.**
- **`user_config_secrets.h`** (gitignored) — `WIFI_NETWORKS`, `WIFI_NETWORK_COUNT`, `API_BASE_URL`. Included at the bottom of `user_config.h`.
- **`user_config_secrets.example.h`** (tracked) — dummy template. A new secret must be added to **both** the real header and the example.

## Directory map

| Path | Purpose |
|------|---------|
| `ePaperConversational.ino` | `setup()`/`loop()`, wake-cause dispatch, PSRAM guard |
| `user_app.{h,cpp}` | LVGL port, state machine, tasks, buttons, touch, deep sleep |
| `ws_client.{h,cpp}` | WebSocket client, hand-rolled JSON, audio buffer + streaming protocol |
| `audio_stream.{h,cpp}` | 512KB PSRAM ring buffer with blocking writes / semaphore backpressure |
| `audio_bsp.{h,cpp}` | ES8311 control, recording task, WAV/PCM/stream playback tasks, beeps |
| `messages.{h,cpp}` | `MSG_ES`/`MSG_EN` tables, `g_lang_index`, `lang_init()`/`lang_toggle()` |
| `wifi_bsp.{h,cpp}` | Multi-WiFi scan/connect, LED, status-bar updates |
| `user_config.h` | All non-secret config |
| `lv_conf.h` | LVGL 9 config — must be copied into the Arduino `lvgl` library |
| `font_montserrat_latin_14.c` | Project font (14px/4bpp, ASCII + Latin-1) |
| `src/ui/screens.cpp` | 7 `create_screen_*()` functions + coalesced receiving-status updater |
| `src/ui/status_bar.cpp` | WiFi + battery status bar |
| `src/display/` | E-paper SPI driver (`epaper_driver_display` class in `epaper_driver_bsp.*`) |
| `src/power/` | GPIO power rails for EPD / audio / VBAT |
| `src/button_bsp/` | Vendored `multi_button` + press detection |
| `src/touch_bsp/` | FT6336 driver |
| `src/i2c_bsp/` | I2C bus init (SDA=47, SCL=48) |
| `src/battery/` | ADC battery measurement |
| `src/codec_board/`, `src/esp_codec_dev/` | Vendored ESP-IDF codec components (esp_codec_dev v1.3.5) |
| `api_client.*`, `minimp3.h` | **Dead** — unused REST client and MP3 decoder |
| `specs/TechnicalSpec.md` | **Stale** design doc (describes REST, not WebSocket) |
