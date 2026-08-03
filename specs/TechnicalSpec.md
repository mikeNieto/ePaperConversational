# Technical and Functional Specification

## Device: Waveshare ESP32-S3-Touch-ePaper-1.54

---

# PART 1: FUNCTIONAL SPECIFICATION

## 1. Overview

Firmware for the Waveshare ESP32-S3-Touch-ePaper-1.54 that operates as a voice-driven conversational interface. The device records spoken messages, transmits them to a backend AI agent over a persistent WebSocket connection, and plays back the agent's audio response while displaying its text on the e-paper screen. The entire conversation loop is hands-free after a button press or screen tap; the device returns to deep sleep after a configurable inactivity period.

---

## 2. User-Facing States and Screens

The device has six application states, each with a corresponding screen rendered via LVGL 9 on the 200×200 e-paper display. The status bar (WiFi indicator and battery gauge) is present on every screen except deep sleep.

### 2.1 State 0 — Connecting

```
+---------------------------------------------+
| WIFI: --                          [bat]XX%  |
|---------------------------------------------|
|                                             |
|           Connecting...                     |
|                                             |
+---------------------------------------------+
```

- Displayed immediately after boot or wake while the WiFi and WebSocket connections are established.
- The on-board LED (GPIO 3) blinks at 200ms intervals during this state.
- When the WebSocket connects, the device emits a wake beep (on user-initiated wake) and transitions to State 1 (Record).
- If WiFi or the WebSocket fail, the device retries automatically via the background `wifi_task` and `ws_task`; the screen remains until a connection is established.

### 2.2 State 1 — Record

```
+---------------------------------------------+
| WIFI: OK                          [bat]XX%  |
|---------------------------------------------|
|                                             |
|             Record Message                  |
|                                             |
+---------------------------------------------+
```

- The idle state. The device waits for user input to start a new recording.

**Physical buttons:**
- **BOOT single-click:** starts recording → State 2 (Listening).
- **PWR single-click:** opens Settings → State 5.
- **PWR long-press (any state):** enters deep sleep.

**Touch (if FT6336 present):**
- Tap anywhere on screen: starts recording → State 2.

### 2.3 State 2 — Listening (Recording in Progress)

```
+---------------------------------------------+
| WIFI: OK                          [bat]XX%  |
|---------------------------------------------|
|                                             |
|             Listening...                    |
|                                             |
+---------------------------------------------+
```

- A start beep plays and the microphone captures audio at 16 kHz, 16-bit stereo into a 1.92 MB PSRAM buffer.
- Maximum recording duration: 30 seconds (buffer capacity). When the buffer fills, the recording stops automatically and is discarded; the device returns silently to State 1.
- The inactivity sleep timer is paused while recording.

**Physical buttons:**
- **BOOT single-click:** stops recording, plays a stop beep, and sends the WAV over WebSocket → State 3 (Receiving). If the recording contains no audio data (WAV header only, 44 bytes), the device returns to State 1 without sending.
- **PWR single-click:** discards the recording without sending, plays a discard beep → State 1.

**Touch:**
- Tap anywhere: same as BOOT single-click (stop and send).

### 2.4 State 3 — Receiving

```
+---------------------------------------------+
| WIFI: OK                          [bat]XX%  |
|---------------------------------------------|
| Transcribing...                             |
| (server-pushed status updates appear here)  |
|                                             |
| (agent text tokens stream in as they arrive)|
|                                             |
| +------------------------------------------+|
```

- The recorded WAV has been sent and the device waits for the backend response over the same WebSocket.
- The screen displays status updates pushed by the server (`transcribing`, `thinking`, `speaking`) mapped to localized labels, followed by the agent's text response streamed token-by-token.
- In parallel, the device may receive a streaming PCM audio response via the WebSocket binary channel (see §7.2).
- The inactivity sleep timer is paused throughout this state.

**Transition triggers (server-driven):**
- `{"type":"done"}` → copies the accumulated agent text and transitions to State 4 (Response).
- `{"type":"error"}` → displays a localized error message for 2 seconds, stops any in-progress playback, frees audio buffers, and returns to State 1.
- WebSocket disconnect → returns to State 0 (Connecting).

**No physical button interaction during this state.**

### 2.5 State 4 — Response

```
+---------------------------------------------+
| WIFI: OK                          [bat]XX%  |
|---------------------------------------------|
| (agent's text response,                     |
|  scrollable, centered, word-wrapped)        |
|                                             |
|                                             |
| +------------------------------------------+|
```

- Displays the agent's full text response. Audio playback (if any) runs concurrently via the streaming or WAV/PCM playback task.
- The inactivity sleep timer is paused while audio is still playing.
- Previous response text and audio buffers from State 3 are preserved so the user can finish reading/hearing.

**Physical buttons:**
- **BOOT single-click:** stops current audio playback, frees the audio buffer and response text, and immediately starts a new recording → State 2 (Listening). This allows rapid back-and-forth conversation.
- **PWR single-click:** stops playback, frees buffers, plays a reconnect beep, and requests a WebSocket reconnect → State 0 (Connecting).

**Touch:**
- Tap anywhere: same as BOOT single-click.

**Automatic transitions:**
- If the WebSocket disconnects and audio has finished playing, the device returns to State 0.

### 2.6 State 5 — Settings

```
+---------------------------------------------+
| WIFI: OK                          [bat]XX%  |
|---------------------------------------------|
| SETTINGS                                    |
|                                             |
| WiFi: <SSID>                                |
| Language: English / Español                 |
|                                             |
+---------------------------------------------+
```

- Displays the currently connected WiFi SSID and the active language.
- The inactivity sleep timer is **not** paused in this state — the device will sleep after 60 seconds of inactivity even while settings are shown.

**Physical buttons:**
- **BOOT single-click:** toggles language (English ↔ Español), re-renders the screen.
- **PWR single-click:** returns to State 1 (Record).

---

## 3. State Machine

```
                     ┌─────────────────────┐
                     │   0. CONNECTING     │◄──────────────────────────┐
                     └─────────┬───────────┘                           │
                               │                                       │
                      EVT_WS_CONNECTED                                 │
                      (+ wake beep if user wake)                       │
                               │                                       │
                               ▼                                       │
                     ┌─────────────────────┐                           │
              ┌─────►│    1. RECORD        │◄────┐                     │
              │      └─────────┬───────────┘     │                     │
              │                │                 │                     │
              │     EVT_START_RECORDING   EVT_EXIT_SETTINGS            │
              │         (BOOT / touch)          (PWR)                  │
              │                │                 │                     │
              │                ▼                 │                     │
              │      ┌─────────────────────┐     │                     │
              │      │   2. LISTENING      │     │                     │
              │      └─────────┬───────────┘     │                     │
              │           │         │            │                     │
              │    EVT_STOP_   EVT_DISCARD/      │                     │
              │    RECORDING   EVT_RECORDING_    │                     │
              │  (valid WAV)   DONE (timeout)    │                     │
              │           │         │            │                     │
              │           │         └────────────┤                     │
              │           │                      │                     │
              │           ▼                      │                     │
              │      ┌─────────────────────┐     │                     │
              │      │   3. RECEIVING      │     │                     │
              │      └─────────┬───────────┘     │                     │
              │           │         │            │                     │
              │    EVT_RESPONSE_  EVT_WS_ERROR/  │                     │
              │    READY (done)   EVT_WS_DISCONNECTED                  │
              │           │         │            │                     │
              │           ▼         ▼            │                     │
              │      ┌─────────────────────┐     │                     │
              │      │   4. RESPONSE       │     │                     │
              │      └──────┬──────┬───────┘     │                     │
              │        BOOT │ PWR  │ WS disc.    │                     │
              │    (EVT_NEXT_│(EVT_ │ + audio    │                     │
              │     MESSAGE) │  WS_ │ done       │                     │
              │              │RECONNECT)         │                     │
              │              │      │            │                     │
              │              │      ▼            │                     │
              │              └───────────────────┘                     │
              │                                                        │
              │      ┌─────────────────────┐                           │
              ├──────┤   5. SETTINGS       │                           │
              │      └─────────┬───────────┘                           │
              │    EVT_TOGGLE_  │                                      │
              │    LANGUAGE     │                                      │
              │    (re-render)  │                                      │
              │                │                                       │
              │   WS disconnect / error from any state ────────────────┘
              │
              └─── PWR long-press from any state → deep sleep
```

**Button actions summary:**

| State | BOOT single-click | PWR single-click | PWR long-press |
|-------|-------------------|------------------|----------------|
| CONNECTING | ignored | ignored | deep sleep |
| RECORD | start recording | open settings | deep sleep |
| LISTENING | stop & send | discard | deep sleep |
| RECEIVING | ignored | ignored | deep sleep |
| RESPONSE | next message | reconnect WS | deep sleep |
| SETTINGS | toggle language | exit settings | deep sleep |

Touch interaction: tap in RECORD → start recording; tap in LISTENING → stop & send; tap in RESPONSE → next message.

---

## 4. Deep Sleep Behaviour

**Entry:**
- An inactivity timer runs for 60 seconds. It is paused in LISTENING, RECEIVING, and RESPONSE (while audio is playing). It is **not** paused in SETTINGS.
- When the timer expires, the device draws the deep sleep screen, waits 500ms, and enters deep sleep.
- PWR long-press from any state bypasses the timer and enters deep sleep immediately.
- Before sleeping, a full e-paper refresh (`EPD_Init()` + `EPD_Display()`) is executed to erase the display and prevent ghosting. A sleep beep plays.

**During sleep:**
- Duration: 3600 seconds (1 hour) via RTC timer wake-up.
- Wake sources: BOOT (GPIO 0) or PWR (GPIO 18) via `EXT1_ANY_LOW`.
- GPIO 17 (VBAT power) is held via `rtc_gpio_hold_en()`.
- Variables in RTC slow memory survive: `boot_count`, `sleep_counter`, `g_lang_index`.

**Wake causes:**

| Cause | Behaviour |
|-------|-----------|
| `ESP_SLEEP_WAKEUP_EXT1` (user pressed BOOT or PWR) | Full initialization: display, WiFi, WebSocket, audio. `sleep_counter` reset to 0. Wake beep queued. |
| `ESP_SLEEP_WAKEUP_TIMER` (auto-wake after 1 hour) | Light path: display only. Increments `sleep_counter`, renders "Sleeping... N" screen, and re-enters deep sleep after 800ms. No WiFi, no audio, no full EPD refresh. |
| First boot (`boot_count == 0`) | Full initialization, `sleep_counter = 0`, no wake beep. |

---

## 5. Status Bar

Rendered as an LVGL layer-top widget, 200×24 px. Visible on all screens except deep sleep.

- **WiFi indicator (left):** displays `WIFI: OK` when connected or `WIFI: --` when disconnected/connecting.
- **Battery indicator (right):** a 4-bar icon plus percentage label (e.g., `[||||] 85%`). Bars fill in 25% increments. The percentage is computed from a calibrated LiPo voltage curve (see §8.2).
- Updated every 30 seconds by the `bat_task`; WiFi status updated every 5 seconds by the `wifi_task`.

---

## 6. Internationalization (i18n)

Two languages are supported: Spanish (`MSG_ES`) and English (`MSG_EN`). The language index is stored in RTC memory (`g_lang_index`) and persists across deep sleep cycles.

Adding a language requires:
1. Defining a `const LangMessages MSG_XX` table with all fields.
2. Appending its pointer to `lang_table[]` in `messages.cpp`.
3. The table count is derived automatically via `sizeof`.

The language is toggled in the Settings screen (BOOT single-click) and initialized at boot from the persisted index.

---

# PART 2: TECHNICAL SPECIFICATION

## 7. Hardware Platform

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32-S3R8 (Xtensa LX7 dual-core, 240 MHz) |
| Flash | 8 MB (QIO mode) |
| PSRAM | Octal SPI, 8 MB (OPI PSRAM mode) |
| Display | E-Paper 1.54", 200×200 px, 1-bit (black/white), SSD1681 controller |
| Touch | FT6336 capacitive (I2C addr 0x38) — detected at runtime; operations degrade gracefully when absent |
| Audio Codec | ES8311 (I2S in/out + I2C control), external PA on GPIO 46 |
| RTC | PCF85063 (I2C addr 0x51) |
| Temp/Humidity | SHTC3 (I2C addr 0x70) |
| Battery | LiPo 1S, ADC1_CH3 (GPIO 4), 1:2 voltage divider |
| Buttons | BOOT (GPIO 0), PWR (GPIO 18) — both active LOW |
| LED | GPIO 3 — active LOW (LOW = on, HIGH = off); `wifi_led_write(true)` means LED on |
| Charging | USB-C with integrated charge controller |

---

## 8. Pin Map

### 8.1 Display — SPI2

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| DC | 10 | Output | Data/Command |
| CS | 11 | Output | Chip Select, active LOW |
| SCK | 12 | Output | SPI Clock |
| MOSI | 13 | Output | MOSI only (no MISO, write-only) |
| RST | 9 | Output | Hardware reset |
| BUSY | 8 | Input | HIGH = busy |
| PWR | 6 | Output | Display power rail, active LOW |

### 8.2 Audio — I2S + Control

| Signal | GPIO | Notes |
|--------|------|-------|
| BCLK | 15 | I2S bit clock |
| WS / LRCLK | 38 | Word select |
| DOUT | 45 | Data out (ESP32 → codec DAC) |
| DIN | 16 | Data in (codec ADC → ESP32) |
| MCLK | 14 | Master clock |
| Audio PWR | 42 | Codec power rail, active LOW |
| PA | 46 | External amplifier enable, active HIGH |

### 8.3 I2C Bus (I2C_NUM_0, 400 kHz)

| Signal | GPIO |
|--------|------|
| SDA | 47 |
| SCL | 48 |

| Device | Address |
|--------|---------|
| PCF85063 (RTC) | 0x51 |
| SHTC3 (sensor) | 0x70 |
| FT6336 (touch) | 0x38 |

### 8.4 Other Pins

| Signal | GPIO | Notes |
|--------|------|-------|
| Touch RST | 7 | Reset pulse HIGH→LOW→HIGH |
| Touch INT | 21 | Interrupt, falling edge, pull-up |
| VBAT PWR | 17 | Battery measurement circuit enable |
| VBAT ADC | 4 | ADC1_CH3, 12 dB attenuation |
| LED | 3 | Active LOW |
| RTC INT | 5 | PCF85063 interrupt |
| BOOT | 0 | Wake source EXT1 |
| PWR | 18 | Wake source EXT1 |

---

## 9. Software Architecture

### 9.1 External Libraries

| Library | Version | Usage |
|---------|---------|-------|
| lvgl | 9.x | UI rendering, configured via project `lv_conf.h` |
| ArduinoWebsockets | — | WebSocket client transport |
| multi_button | vendored in `src/button_bsp/` | Multi-press button detection |
| esp_codec_dev | v1.3.5, vendored in `src/esp_codec_dev/` | ES8311 codec control |
| codec_board | vendored in `src/codec_board/` | ES8311 board-level init |

### 9.2 File Map

```
ePaperConversational/
├── ePaperConversational.ino    # setup() / loop()
├── user_app.h / .cpp           # State machine, LVGL port, tasks, touch, deep sleep
├── user_config.h               # All non-secret constants (pins, timings, buffer sizes)
├── user_config_secrets.h       # Gitignored: WiFi credentials, API_BASE_URL
├── ws_client.h / .cpp          # WebSocket client, JSON parsing, audio buffer mgmt
├── audio_stream.h / .cpp       # 512 KB PSRAM ring buffer for streaming PCM
├── audio_bsp.h / .cpp          # ES8311 control, recording, playback, beeps
├── messages.h / .cpp           # i18n string tables (MSG_ES, MSG_EN)
├── wifi_bsp.h / .cpp           # Multi-WiFi scan/connect, LED, status-bar updates
├── lv_conf.h                   # LVGL 9 configuration (copied into Arduino library)
├── font_montserrat_latin_14.c  # Project font (14 px, 4bpp, ASCII + Latin-1)
├── api_client.h / .cpp         # Dead code — unused REST client, compiled but not linked
├── minimp3.h                   # Dead code — unused MP3 decoder
├── src/
│   ├── display/                # E-paper SPI driver (epaper_driver_display)
│   ├── power/                  # GPIO power rails for EPD / audio / VBAT
│   ├── button_bsp/             # Vendored multi_button + press detection
│   ├── i2c_bsp/                # I2C bus init (SDA=47, SCL=48)
│   ├── touch_bsp/              # FT6336 driver
│   ├── battery/                # ADC battery measurement
│   ├── codec_board/            # Vendored codec board init
│   ├── esp_codec_dev/          # Vendored esp_codec_dev v1.3.5
│   └── ui/
│       ├── screens.h / .cpp    # Seven create_screen_*() functions + status coalescing
│       └── status_bar.h / .cpp # WiFi + battery status bar
└── specs/
    └── TechnicalSpec.md        # This document
```

### 9.3 FreeRTOS Tasks

All tasks are pinned to Core 1.

| Task | Priority | Stack | Function |
|------|----------|-------|----------|
| `LVGL` | 4 | 8 KB | LVGL timer handler + render loop; owns the LVGL mutex |
| `ws_task` | 3 | 20 KB | WebSocket connect/poll/send, JSON parsing, binary→ring-buffer write; 20 KB stack needed for `String` URL parsing |
| `stream_task` | 5 | 8 KB | Streaming audio playback (created dynamically on `audio_start`, exits on `audio_end`/timeout) |
| `state_task` | 3 | 8 KB | Consumes `state_queue`, drives all screen transitions |
| `btn_task` | 3 | 4 KB | Polls boot/pwr event groups, translates button events to `AppEvent` on `state_queue` |
| `touch_task` | 3 | 4 KB | FT6336 touch processing via lock-free ring buffer; only created if touch is detected |
| `wifi_task` | 2 | 4 KB | WiFi reconnect loop, status bar WiFi update |
| `bat_task` | 1 | 4 KB | Periodic battery measurement, status bar battery update |
| `sleep_timer` | 1 | 4 KB | 60-second inactivity countdown |

Additional transient tasks:
- `rec_task` (prio 5, 8 KB): recording loop, exits when stopped or buffer full.
- `wav_task` (prio 5, 8 KB): WAV/PCM playback loop, exits when done or stopped.

### 9.4 Inter-Task Communication

| Mechanism | Usage |
|-----------|-------|
| `QueueHandle_t state_queue` | AppEvent messages (12 event types) for state transitions |
| `QueueHandle_t ws_cmd_queue` | WsCmd (send_audio / reconnect) — isolates other tasks from the socket |
| `EventGroupHandle_t boot_groups` | BOOT button events (single, double, long, up) |
| `EventGroupHandle_t pwr_groups` | PWR button events (single, double, long, up) |
| `EventGroupHandle_t wifi_event_group` | WiFi connected/disconnected flags |
| `EventGroupHandle_t touch_event_group` | Touch tap detected flag |
| `SemaphoreHandle_t lvgl_mux` | Mutex — must be held before any LVGL widget access |
| `SemaphoreHandle_t space_sem` (in `audio_stream`) | Binary semaphore — reader signals writer when ring buffer space frees |
| Task notifications | `stream_task` reader wake-up; `sleep_timer` activity feed; `wav_task` stop signal |

---

## 10. LVGL Configuration

- **Version:** 9.x (compile-time check enforced).
- **Display resolution:** 200×200, colour format RGB565.
- **Render mode:** `LV_DISPLAY_RENDER_MODE_FULL` — always redraws the full area; the flush callback thresholds each pixel.
- **Buffers:** Two 80 KB buffers (200×200×2 bytes), allocated in SPIRAM with fallback to internal RAM.
- **Flush callback:** `EPD_Clear()` → for each RGB565 pixel, threshold at `0x7FFF` to produce 1-bit black/white → `EPD_DrawColorPixel()` → `EPD_DisplayPart()` (partial refresh) → `lv_display_flush_ready()`.
- **Tick source:** `esp_timer` at 5 ms → `lv_tick_inc(5)`.
- **Touch input:** A lock-free ring buffer of 32 `TouchSample` structs fed by the `touch_task` and consumed by `lvgl_touch_read_cb`. Sets `data->continue_reading` when more samples are pending.
- **Full refresh (`EPD_Init()` + `EPD_Display()`)** is reserved exclusively for deep sleep entry. All runtime updates use partial refresh.
- **Disabled in `lv_conf.h`:** unused widgets, themes, layouts, decoders, display drivers, and the built-in Montserrat 14 font. The project font is `font_montserrat_latin_14.c`.
- LVGL themes are disabled, so styled objects must set opacity explicitly (e.g., `LV_OPA_COVER`).

---

## 11. WebSocket Protocol

A single persistent WebSocket connection to `ws://<host>:<port>/ws`, where `<host>` and `<port>` are parsed from the `http://host:port` URL stored in `API_BASE_URL`.

`WiFi.setSleep(false)` is called inside `ws_task` to prevent modem sleep from disrupting real-time audio streaming.

### 11.1 Client → Server

**Binary frame:** The recorded WAV file (44-byte header + 16-bit stereo PCM at 16 kHz), sent as a single binary message.

**Text frame (immediately after binary):**
```json
{"type":"audio_end"}
```

These two messages are sent sequentially through `ws_cmd_queue`, which is consumed by `ws_task` to ensure only the WebSocket task touches the socket.

### 11.2 Server → Client

The server responds with a mix of text and binary frames on the same connection. The hand-rolled JSON parser in `ws_client.cpp` uses `String::indexOf` with manual `\uXXXX` UTF-16 surrogate pair decoding — no ArduinoJson dependency.

#### Text frame types

| `type` | Purpose | Fields |
|--------|---------|--------|
| `audio_start` | Begins streaming PCM playback | `sample_rate` (int), `channels` (int), `bits` (int) — default 24000/1/16 |
| `audio_end` | Signals end of audio stream | — |
| `status` | Updates the receiving screen status line | `state`: `"transcribing"`, `"thinking"`, or `"speaking"` (mapped to localized labels) |
| `token` | Streaming text token | `content`: appended to the accumulated agent text, displayed in real time |
| `text` | Full text content | `content`: replaces or sets the agent text |
| `done` | Final response frame | — triggers `EVT_RESPONSE_READY`, copies accumulated text to `g_agent_text` |
| `error` | Server-side error | `message`: displayed for 2 s; triggers `EVT_WS_ERROR` |

#### Binary frames

Raw PCM audio chunks (~32 KB each), written directly into the 512 KB ring buffer. If the buffer is full, `ws_task` blocks (up to `STREAM_TIMEOUT_MS`) to apply TCP backpressure.

### 11.3 Streaming Sequence (Typical Conversation)

```
Client                              Server
  │                                    │
  ├── [BIN] WAV file ────────────────►│
  ├── {"type":"audio_end"} ──────────►│
  │                                    │
  │◄── {"type":"status","state":"transcribing"}
  │◄── {"type":"status","state":"thinking"}
  │◄── {"type":"audio_start","sample_rate":24000,...}
  │◄── [BIN] PCM chunk 1
  │◄── [BIN] PCM chunk 2
  │◄── {"type":"status","state":"speaking"}
  │◄── {"type":"token","content":"Hello"}
  │◄── [BIN] PCM chunk 3
  │◄── {"type":"token","content":", how can"}
  │◄── [BIN] PCM chunk 4
  │◄── {"type":"audio_end"}
  │◄── {"type":"token","content":" I help?"}
  │◄── {"type":"done"}
  │                                    │
  ▼                           STATE_RESPONSE
```

The server must pace binary chunks at approximately real-time rate (~48 KB/s for 24 kHz mono 16-bit). Bursting faster than the playback task consumes data causes the ring buffer to fill and `ws_task` to block.

### 11.4 Fallback: Lone Binary WAV/PCM

If the server sends a binary payload without a preceding `audio_start` message, the device treats it as a complete WAV or raw PCM buffer and plays it via `audio_play_wav_start()` / `audio_play_pcm_start()` after transitioning to STATE_RESPONSE. This path exists for backwards compatibility.

---

## 12. Audio Pipeline

### 12.1 Recording

```
Microphone → ES8311 ADC → I2S DIN → rec_task (Core 1, prio 5, 8 KB stack)
    → esp_codec_dev_read() in 1024-byte chunks
    → PSRAM buffer (1.92 MB = 16000 Hz × 2 ch × 2 bytes × 30 s)
    → On stop: build 44-byte WAV header in-place at buffer start
    → ws_send_audio() pushes the full WAV to ws_cmd_queue
```

- Format: 16 kHz, 16-bit, stereo PCM.
- The codec is opened at 16 kHz stereo for recording. Recording and playback cannot share the codec simultaneously; the codec is re-opened for playback with the appropriate format.
- Buffer is freed after sending (`audio_free_recording_buffer()` from `ws_send_audio`).

### 12.2 Streaming Audio Playback

```
WebSocket binary frame → ws_task (onMessageCallback, Core 1)
    → stream_buf_write() → 512 KB ring buffer (PSRAM)
    → If full: binary semaphore blocks writer up to STREAM_TIMEOUT_MS (10 s)
    → stream_task (Core 1, prio 5, 8 KB stack)
        → Waits for STREAM_MIN_FILL_BYTES (24 KB, ~0.5 s at 24 kHz mono) or audio_end signal
        → Opens ES8311 at negotiated sample rate/channels/bits
        → Reads 1 KB (frame-aligned) chunks from ring buffer
        → esp_codec_dev_write() — blocking I2S write, retries transient errors
        → Loops until: end signal + buffer drained, stop flag, or STREAM_TIMEOUT_MS with no data
```

Key design decisions:
- The blocking I2S write in the playback task is what yields CPU time to `ws_task`. No artificial delay is injected.
- The ring buffer is a single-producer/single-consumer design with a binary semaphore for backpressure.
- On `audio_end`, the server signals the stream end; the playback task drains remaining data, closes the codec, and frees the ring buffer.

### 12.3 WAV / Raw PCM Fallback Playback

Non-streamed playback (beeps and the lone-binary fallback) uses a `wav_playback_task`:
- Parses the WAV header to extract sample rate, channels, and bit depth.
- Opens the ES8311 codec at the parsed format.
- Writes 1 KB chunks with a 5 ms inter-chunk delay, respecting `wav_stop_flag` and `wav_replay_flag`.
- The replay flag is set by `audio_play_wav_replay()` but is not currently wired to any user action.
- Raw PCM (without WAV header) is handled via `audio_play_pcm_start()` with explicit format parameters.

### 12.4 Beeps

Six beep types are defined as short WAV waveforms generated in heap/PSRAM at 24 kHz mono 16-bit:

| Beep | Event | Waveform |
|------|-------|----------|
| START | Recording begins | 800 Hz, 130 ms |
| STOP | Recording stopped | 500→800 Hz two-tone, 150 ms |
| DISCARD | Recording discarded | 300 Hz, 330 ms |
| RECONNECT | WebSocket reconnect requested | 1000 Hz, 330 ms |
| SLEEP | Entering deep sleep | 900→600 Hz two-tone, 200 ms |
| WAKE | User-initiated wake | 600→900 Hz two-tone, 200 ms |

All waveforms begin with 50–70 ms of silence to prime the I2S TX DMA. Without this leading silence, short beeps are inaudible.

**Always use `audio_beep_play_standalone()`**, not `audio_beep_play()`. The latter writes directly to an already-open codec and fails after a codec close/reopen cycle because the ES8311 DAC path is not re-enabled.

---

## 13. WiFi

### 13.1 Multi-Network Scanning

Credentials are defined in `user_config_secrets.h` as `WIFI_NETWORKS` (an array of `{ssid, password}` pairs) and `WIFI_NETWORK_COUNT`.

`wifi_connect_best()`:
1. Early-returns if already `WL_CONNECTED` (prevents scan-induced WebSocket drops).
2. Scans visible APs, filters to known SSIDs, and picks the strongest RSSI.
3. Connects with a timeout of `WIFI_CONNECT_TIMEOUT_MS` (10 s).
4. If no known network is found, returns; `wifi_task` rescans every 5 seconds.

### 13.2 Reconnection Loop

The `wifi_task` (Core 1, prio 2) polls every 5 seconds:
- If connected and the event group does not yet reflect it, sets `WIFI_BIT_CONNECTED` and logs the IP.
- If disconnected, clears the connected bit, sets `WIFI_BIT_DISCONNECTED`, and calls `wifi_connect_best()`.
- Updates the status bar WiFi label under the LVGL mutex.

`wifi_is_connected()` checks `WIFI_BIT_CONNECTED` in the event group, not `WiFi.status()` directly.

---

## 14. Battery Measurement

- **ADC:** ADC1_CH3 (GPIO 4), 12-bit, 12 dB attenuation, curve-fitting calibration.
- **Sampling:** 16 samples at 2 ms intervals; sorted, trimmed (2 extremes removed), averaged.
- **Divider:** 1:2 resistor divider → measured voltage × 2.0.
- **Percentage mapping:** non-linear LiPo curve (piecewise linear interpolation):

| Voltage | % |
|---------|---|
| 4.12 V | 100 |
| 4.08 V | 95 |
| 4.02 V | 90 |
| 3.95 V | 80 |
| 3.88 V | 70 |
| 3.82 V | 60 |
| 3.76 V | 50 |
| 3.70 V | 40 |
| 3.65 V | 30 |
| 3.55 V | 15 |
| 3.30 V | 0 |

The `bat_task` measures every 30 seconds and updates the status bar widgets.

---

## 15. Button Handling

The vendored `multi_button` library detects single-click, double-click, long-press, and press-up events on BOOT (GPIO 0) and PWR (GPIO 18).

**Important bit-order difference:**

| Event | BOOT bit | PWR bit |
|-------|----------|---------|
| Single | 0 | 0 |
| Long | 1 | 1 |
| Up | 2 | 3 |
| Double | 3 | 2 |

Always use the named constants (`BOOT_BIT_SINGLE`, `PWR_BIT_DOUBLE`, etc.) with the `BTN_GET(ev, BIT)` macro.

The `btn_task` polls event groups every 200 ms, translates bits to `AppEvent` messages on `state_queue`, and handles PWR long-press (deep sleep) immediately before the per-state dispatch.

---

## 16. Touch Detection and Handling

**Detection at boot:**
1. Pulse the FT6336 RST pin (GPIO 7): HIGH → LOW → HIGH with 10 ms/50 ms delays.
2. Probe I2C address 0x38.
3. If found, create the `touch_task` and `touch_event_group`; if not, `hasTouch = false`.

**Touch task:**
- Waits on a queue fed by the GPIO ISR on the FT6336 INT pin (GPIO 21, falling edge).
- Reads touch coordinates via FT6336 driver.
- Pushes samples to a lock-free ring buffer (32 entries) consumed by LVGL's `lvgl_touch_read_cb`.
- Fires `TOUCH_BIT_TAP` on the `touch_event_group` (used by screens for tap detection).
- Includes a 3-read debounce on release to suppress spurious lift-off events.

When touch is absent, the UI functions identically via physical buttons.

---

## 17. Inactivity Timer

A `sleep_timer` task waits on a task notification with a 60-second timeout (`INACTIVITY_TIMEOUT_MS`). Any user interaction (button press, touch, or `activity_feed()` call) resets the timer via `xTaskNotifyGive()`.

The timer is **paused** (continues looping without sleeping) when:
- `STATE_LISTENING`
- `STATE_RECEIVING`
- `STATE_RESPONSE` **and** audio is still playing (`audio_wav_is_playing()`)

It is **not paused** in `STATE_SETTINGS` — the device will sleep after 60 s even while viewing settings.

---

## 18. Deep Sleep Implementation

Two entry functions:

**`enter_deep_sleep()`** — full path:
1. Play sleep beep.
2. Full EPD refresh (`EPD_Init()` + `EPD_Display()`) for a clean white screen.
3. Configure EXT1 wake-up on GPIO 0 and GPIO 18 (ANY_LOW).
4. Hold GPIO 17 via `rtc_gpio_hold_en()`.
5. Set 3600 s timer wake-up.
6. 500 ms delay, then `esp_deep_sleep_start()`.

**`enter_deep_sleep_light()`** — timer auto-wake path:
1. Skip the EPD refresh (display was already rendered with the sleep counter).
2. Same EXT1 and timer configuration.
3. `esp_deep_sleep_start()`.

---

## 19. Boot Sequence

The `setup()` function in `ePaperConversational.ino` handles four wake scenarios:

### 19.1 First Boot (`boot_count == 0`)

1. Enable PSRAM allocation for blocks ≥256 bytes (`heap_caps_malloc_extmem_enable(256)`).
2. Initialize language from `g_lang_index` (defaults to 0, Spanish).
3. `sleep_counter = 0`.
4. `user_app_init()` — initializes serial, power rails, display driver, LVGL port, button detection, touch detection (if present), WiFi (blocking `wifi_connect_best`), battery, inactivity timer, audio codec, and WebSocket.
5. `lvgl_port_init()` + `user_ui_init()` inside `user_app_init()`: loads the "Connecting" screen before WiFi blocks.
6. `status_bar_set_visible(true)`.
7. If the application state has already advanced past `STATE_CONNECTING` (e.g., WiFi was fast), call `switch_state(g_app_state)` to correct the screen.

### 19.2 EXT1 Wake (User Pressed BOOT or PWR)

1. `sleep_counter = 0`.
2. `g_play_wake_beep = true` (consumed on `EVT_WS_CONNECTED`).
3. Full `user_app_init()` as above.

### 19.3 Timer Wake

1. `sleep_counter++`.
2. Display-only init (`display_light_init()` + `lvgl_port_init()`).
3. Render "Sleeping... N" screen.
4. 800 ms delay, then `enter_deep_sleep_light()`.

### 19.4 Other Wake

Same path as EXT1 but without resetting `sleep_counter`.

### Idempotency

`lvgl_port_init()` and `user_ui_init()` use static guards to ensure they execute only once. This is critical because the timer wake path calls `lvgl_port_init()` standalone after `display_light_init()` has already initialized the display.

---

## 20. Persistent State (RTC Slow Memory)

| Variable | Type | Purpose |
|----------|------|---------|
| `boot_count` | `int` | Total number of boots (debug) |
| `sleep_counter` | `int` | Consecutive timer auto-wakes without user interaction |
| `g_lang_index` | `int` | Index into `lang_table[]` (0 = Spanish, 1 = English) |

None of these are reset to defaults on wake; the boot-time logic in `setup()` explicitly sets `sleep_counter = 0` on user wake and `boot_count = 0` on the very first boot.

---

## 21. Screen Implementation Details

Each `create_screen_*()` function:
1. Creates a new `lv_obj_t *screen = lv_obj_create(NULL)` with white background and `LV_OPA_COVER`.
2. Removes the `LV_OBJ_FLAG_SCROLLABLE` flag (the screen itself does not scroll; scrollable text areas use inner containers).
3. Calls `create_status_bar(screen)` for all screens except deep sleep.
4. Returns the screen object.

The state machine in `switch_state()`:
1. Takes the LVGL mutex.
2. Records the current active screen.
3. For STATE_RESPONSE, saves the receiving screen's scroll position to carry it forward.
4. Calls the appropriate `create_screen_*()`.
5. Loads the new screen, runs `lv_timer_handler()`, and deletes the old screen.
6. Releases the mutex.
7. For STATE_RESPONSE, if audio is not already playing, checks for a fallback WAV/PCM buffer and starts playback.

### 21.1 Status Text Coalescing

The receiving screen uses a coalescing mechanism to decouple WebSocket text updates from e-paper rendering:

- `queue_screen_receiving_status()` (called from `ws_task`) acquires a mutex briefly, copies the new status text into `pending_receiving_status`, and sets a flag.
- An LVGL timer at 50 ms checks the flag, applies the label update, and preserves the scroll position.
- This prevents the WebSocket thread from ever blocking on an e-paper partial refresh (~300 ms).

The response screen passes the scroll position from the receiving screen to maintain visual continuity.

### 21.2 Status Bar Singleton

`create_status_bar()` creates the bar on `lv_layer_top()` and returns the same object on subsequent calls. This prevents multiple bars from stacking when screens are replaced. `status_bar_set_visible()` toggles the `LV_OBJ_FLAG_HIDDEN` flag.

---

## 22. Error Handling

| Scenario | Action |
|----------|--------|
| WiFi connection failure | `wifi_task` retries every 5 s. Status bar shows `WIFI: --`. |
| WebSocket connection failure | `ws_task` retries every 3 s. Screen remains at Connecting. |
| WebSocket disconnect during conversation | `EVT_WS_DISCONNECTED` → transition to Connecting. Playback and audio buffers are stopped/freed. |
| Server `error` message | Localized error displayed for 2 s, audio playback stopped, return to Record. |
| Request timeout (10 min) | `EVT_WS_ERROR` → stop playback, free buffers, return to Record. |
| PSRAM allocation failure (recording) | Logged as fatal, device restarts via `ESP.restart()`. |
| PSRAM allocation failure (streaming) | Write returns false, chunk lost. |
| I2S write transient error | Playback task retries with 2 ms backoff for up to 1 s. |

---

## 23. Configuration Constants

All non-secret constants in `user_config.h`. Secrets in `user_config_secrets.h` (gitignored; `user_config_secrets.example.h` provided as template).

| Constant | Default | Description |
|----------|---------|-------------|
| `EPD_WIDTH` / `EPD_HEIGHT` | 200 | Display dimensions |
| `LVGL_SPIRAM_BUFF_LEN` | 80000 | 200×200×2 bytes per LVGL buffer |
| `EXAMPLE_LVGL_TICK_PERIOD_MS` | 5 | LVGL tick interval |
| `EXAMPLE_LVGL_TASK_MIN_DELAY_MS` | 20 | Min LVGL task loop delay |
| `EXAMPLE_LVGL_TASK_MAX_DELAY_MS` | 500 | Max LVGL task loop delay |
| `STREAM_BUF_SIZE` | 524288 | Ring buffer capacity (512 KB) |
| `STREAM_MIN_FILL_BYTES` | 24000 | Pre-buffering threshold (~0.5 s at 24 kHz) |
| `STREAM_TIMEOUT_MS` | 10000 | Max wait for buffer fill/data |
| `WS_REQUEST_TIMEOUT_MS` | 600000 | Request timeout (10 min) |
| `AGENT_TEXT_SIZE` | 4096 | Max agent response text |
| `_WS_CONFIG_MAX_MESSAGE_SIZE` | 524288 | WebSocket max message (512 KB) |
| `INACTIVITY_TIMEOUT_MS` | 60000 | Idle timeout before sleep (60 s) |
| `SLEEP_DURATION_SEC` | 3600 | Deep sleep duration (1 hour) |
| `WIFI_CONNECT_TIMEOUT_MS` | 10000 | WiFi connection timeout |
| `BATTERY_FULL_VOLTAGE` | 4.12 | Full charge voltage |
| `BATTERY_EMPTY_VOLTAGE` | 3.30 | Empty voltage |
| `REC_BUFFER_SIZE` | 1920000 | Recording buffer (1.92 MB, 30 s) |

---

## 24. Non-Obvious Gotchas

1. **`user_config.h` must be included before `<ArduinoWebsockets.h>`** — it defines `_WS_CONFIG_MAX_MESSAGE_SIZE` (512 KB). Reversing the order silently reverts to the library's small default and truncates PCM chunks.

2. **LED is active LOW** — `LOW` = on, `HIGH` = off. `wifi_led_write(true)` internally inverts: `gpio_set_level(LED_PIN, on ? 0 : 1)`.

3. **PWR button bit order differs from BOOT** — `PWR_BIT_DOUBLE = 2`, `PWR_BIT_UP = 3` vs `BOOT_BIT_UP = 2`, `BOOT_BIT_DOUBLE = 3`. Always use named constants.

4. **Beeps need leading silence** — ~50–70 ms at the start of every beep waveform primes the I2S TX DMA. Without it, short beeps are inaudible.

5. **Use `audio_beep_play_standalone()`** — the non-standalone variant writes to an already-open codec and fails after a close/reopen cycle.

6. **LVGL themes disabled** — opacity must be set explicitly (e.g., `LV_OPA_COVER` for battery fill bars).

7. **`API_BASE_URL` uses `http://`** even though transport is WebSocket. `ws_client.cpp` parses host and port from it and builds `ws://host:port/ws`.

8. **`ws_task` stack is 20 KB** — needed for `String` URL parsing and large stack-local variables. Do not reduce it.

9. **Arduino IDE compiles everything** — `api_client.cpp` and `minimp3.h` are compiled even though no live code includes them. Do not extend dead files.

10. **Partial refresh everywhere** — full refresh (`EPD_Init()` + `EPD_Display()`) only in `enter_deep_sleep()`.

---

*Document version 2.0 — matches firmware v1.0.0, updated to reflect WebSocket streaming architecture.*
