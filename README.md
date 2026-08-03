# ePaper Conversational

Firmware for the [Waveshare ESP32-S3-Touch-ePaper-1.54](https://www.waveshare.com/esp32-s3-touch-epaper-1.54.htm) that turns the device into a voice-driven AI conversational interface. Press a button, speak, and get a spoken text response rendered on the e-paper display — all over a persistent WebSocket connection.

## Features

- **Push-to-talk voice interface** — single button press or screen tap to record, another to send
- **Real-time streaming audio** — PCM playback starts within ~0.5 s of the server's first binary chunk via a 512 KB PSRAM ring buffer
- **Live transcription display** — server-pushed status updates and agent text tokens stream onto the e-paper screen as they arrive
- **Multi-WiFi** — scans for any configured SSID and connects to the strongest one
- **Deep sleep** — enters 1-hour sleep after 60 s of inactivity; wakes on button press. Displays consecutive sleep cycle count on auto-wake
- **Bilingual UI** (Spanish / English) with language persistence across deep sleep cycles
- **Touch-capable** — auto-detects the FT6336 touch controller; falls back to physical buttons when absent
- **Audio feedback beeps** — distinct tones for start recording, stop, discard, reconnect, sleep, and wake
- **Battery monitoring** — LiPo curve-calibrated percentage with a 4-bar icon on the status bar

## Hardware

| Component | Detail |
|-----------|--------|
| Board | Waveshare ESP32-S3-Touch-ePaper-1.54 |
| MCU | ESP32-S3R8, dual-core 240 MHz |
| Flash | 8 MB (QIO) |
| PSRAM | 8 MB OPI (mandatory) |
| Display | 1.54" e-paper, 200×200 px, SSD1681 |
| Audio | ES8311 codec, built-in MEMS mic + speaker via external PA |
| Touch | FT6336 capacitive (auto-detected) |
| Battery | LiPo 1S on JST connector |

## Quick Start

### 1. Clone

```bash
git clone <repo-url>
cd ePaperConversational
```

### 2. Set up secrets

```bash
cp user_config_secrets.example.h user_config_secrets.h
```

Edit `user_config_secrets.h` with your WiFi credentials and backend URL:

```cpp
#define WIFI_NETWORKS \
    {"YourSSID", "your_password"}, \
    {"OtherSSID", "other_password"}

#define WIFI_NETWORK_COUNT 2

#define API_BASE_URL "http://192.168.1.100:8000"
```

> `API_BASE_URL` uses `http://` syntax even though transport is WebSocket. The firmware parses host and port from it and connects to `ws://host:port/ws`.

### 3. Arduino IDE setup

Install the **esp32** platform by Espressif Systems through Boards Manager. The official stable package index is:

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Use a current stable Arduino-ESP32 3.x release. The menu names below follow the ESP32-S3 board definition in that series. Older core versions may combine or omit some entries; use the equivalent value described in the notes below.

Select the board first:

```text
Tools > Board > esp32 > ESP32S3 Dev Module
```

Then select the following values under `Tools`:

| Tools menu | Select | Required | Why |
|------------|--------|----------|-----|
| **Port** | COM port exposed by the board | Yes | Select the port that appears when the board is connected. It can change after the first native-USB reset. |
| **USB Mode** | **Hardware CDC and JTAG** | Yes | Enables the native USB CDC serial interface used by the firmware's `Serial` output. |
| **USB CDC On Boot** | **Enabled** | Yes | Required for the native USB serial monitor. |
| **USB Firmware MSC On Boot** | **Disabled** | Yes | The firmware does not use USB mass storage. |
| **USB DFU On Boot** | **Disabled** | Yes | The firmware does not use USB DFU mode. |
| **Upload Mode** | **UART0 / Hardware CDC** | Yes | Correct upload mode for Hardware CDC and JTAG. |
| **CPU Frequency** | **240MHz (WiFi)** | Yes | Provides the intended performance margin for WiFi, audio, LVGL, and e-paper refreshes. |
| **Flash Mode** | **QIO 80MHz** | Yes | Matches the board's QIO flash configuration. Do not select `OPI 80MHz`; that is an OPI flash mode, not OPI PSRAM. |
| **Flash Size** | **8MB (64Mb)** | Yes | Matches the board hardware. |
| **Partition Scheme** | **Huge APP (3MB No OTA/1MB SPIFFS)** | Yes | The firmware does not use OTA or a filesystem and the image is close to the default app-size limit. |
| **PSRAM** | **OPI PSRAM** | Yes | Mandatory. The sketch stops at compile time unless `BOARD_HAS_PSRAM` is defined. |
| **Arduino Runs On** | **Core 1** | Recommended | Keeps Arduino runtime work on the same core used by the firmware tasks. |
| **Events Run On** | **Core 1** | Recommended | Matches the project's single-core task placement. |
| **Core Debug Level** | **None** | Recommended | Keeps debug framework overhead out of the flash image. The firmware's own `Serial.printf()` logs remain available. |
| **Upload Speed** | **921600** | Recommended | Fast and normally reliable. Use `115200` if uploads are unstable. |
| **Erase All Flash Before Sketch Upload** | **Disabled** | Recommended | Preserves normal flash contents. Enable it once only when deliberately changing partition layouts or cleaning corrupted flash. |
| **JTAG Adapter** | **Disabled** | Recommended | The project does not use OpenOCD/JTAG debugging. |
| **Zigbee Mode** | **Disabled** | Recommended | The firmware uses WiFi and WebSocket, not Zigbee. |

#### Important Arduino IDE details

- The board has **8 MB of flash**, but the default Arduino partition can expose only about 1.2-1.3 MB to the application. Select **Huge APP (3MB No OTA/1MB SPIFFS)** before compiling; do not use the default 4 MB partition for normal project builds.
- The board uses **QIO flash with OPI PSRAM**. These are two independent settings: choose `Flash Mode: QIO 80MHz` and `PSRAM: OPI PSRAM`.
- The project pins its FreeRTOS tasks to **Core 1**. Selecting `Arduino Runs On: Core 1` and `Events Run On: Core 1` keeps the Arduino runtime aligned with that architecture.
- `Upload Speed` controls flashing only. Set the Serial Monitor separately to **115200 baud**, which matches `Serial.begin(115200)` in the firmware.
- `USB Firmware MSC On Boot`, `USB DFU On Boot`, and `JTAG Adapter` must remain disabled. They are not used by this project and some of them require a different USB mode.
- Some Arduino-ESP32 versions show `QIO` and a separate `Flash Frequency` menu instead of `QIO 80MHz`. In that case select `Flash Mode: QIO` and `Flash Frequency: 80MHz`.
- The ESP32-S3 board definition does not expose separate `Flash Frequency`, `Memory Type`, `Pin Numbering`, or `Chip Variant` menus in current Arduino-ESP32 3.x releases. Leave automatically managed settings unchanged when those menus are absent.
- The instructions assume that the board's USB connector is used for native USB. If the board is connected through a separate USB-to-UART adapter, disable USB CDC on boot and use the adapter's serial port instead.

### 4. Install libraries

Only two external Arduino libraries are needed:

| Library | Version | Notes |
|---------|---------|-------|
| **lvgl** | 9.x | Copy the project `lv_conf.h` into the library folder |
| **ArduinoWebsockets** | latest | — |

All other dependencies (`multi_button`, `esp_codec_dev` v1.3.5, `codec_board`) are vendored under `src/`.

**Critical:** After installing `lvgl`, replace its `lv_conf.h` with the project's `lv_conf.h`. The project config disables unused widgets, themes, fonts, and drivers to keep the binary under the 1.31 MB app partition limit.

### 5. Compile & flash

Open `ePaperConversational.ino` in Arduino IDE, hit **Upload**. The serial monitor (115200 baud) will show:

```
ePaperConversational v1.0.0
PSRAM free: ...  DRAM free: ...
Audio codec initialized: ES8311 OK
Display init complete.
```

## Usage

### Button controls

| Button | State | Action |
|--------|-------|--------|
| **BOOT** click | Record | Start recording |
| **BOOT** click | Listening | Stop recording and send |
| **BOOT** click | Response | Start new recording (next message) |
| **BOOT** click | Settings | Toggle language |
| **PWR** click | Record | Open settings |
| **PWR** click | Listening | Discard recording |
| **PWR** click | Response | Reconnect WebSocket |
| **PWR** click | Settings | Exit settings |
| **PWR** long | Any | Enter deep sleep |

Touch (if available): tap to start/stop recording, same as BOOT click.

### Conversation flow

1. Wake the device with **BOOT** or **PWR**.
2. Wait for `WIFI: OK` and the **Record Message** screen.
3. Press **BOOT** (or tap) → **Listening...** — speak your message.
4. Press **BOOT** (or tap) again to stop. The recording is sent to the backend.
5. The screen shows live transcription status, then the agent's text response.
6. Audio plays through the speaker as soon as the server starts streaming PCM.
7. Press **BOOT** to send another message, or let the device auto-sleep after 60 s.

### Deep sleep

- 60 s inactivity → deep sleep with full screen clear.
- Wakes every hour automatically to show a persistent sleep counter ("Sleeping... N") and then re-enters sleep.
- **PWR long-press** from any screen enters deep sleep immediately.

## How It Works

### Architecture

```
┌─────────┐    ┌──────────┐    ┌──────────────┐    ┌──────────┐
│  MIC    │───▶│ ES8311   │───▶│ rec_task     │───▶│ PSRAM    │
│         │    │ ADC      │    │ (16kHz/2ch)  │    │ 1.92 MB  │
└─────────┘    └──────────┘    └──────┬───────┘    └────┬─────┘
                                      │ WAV header       │
                                      ▼                  ▼
                               ┌──────────┐    ┌──────────────┐
                               │ ws_task  │───▶│ WebSocket    │
                               │ (Core 1) │    │ ws://host/ws │
                               └────┬─────┘    └──────┬───────┘
                                    │                  │
                              ┌─────▼──────┐    ┌──────▼───────┐
                              │ JSON       │    │ Binary PCM   │
                              │ status/    │    │ chunks       │
                              │ token/text │    │ (~32 KB)     │
                              └─────┬──────┘    └──────┬───────┘
                                    │                  │
                              ┌─────▼──────┐    ┌──────▼───────┐
                              │ LVGL       │    │ Ring buffer  │
                              │ display    │    │ 512 KB PSRAM │
                              └────────────┘    └──────┬───────┘
                                                       │
                                                       ▼
                                                ┌──────────┐
                                                │ stream_  │
                                                │ task     │
                                                └────┬─────┘
                                                     │ I2S
                                                     ▼
                                                ┌──────────┐
                                                │ ES8311   │
                                                │ DAC → PA │
                                                │ → Speaker│
                                                └──────────┘
```

### WebSocket protocol

The device maintains a **single persistent WebSocket** to the backend. No REST calls, no polling, no separate download channel.

**Client → Server:**
1. Binary frame: complete WAV file (~1.92 MB max)
2. Text frame: `{"type":"audio_end"}`

**Server → Client** (interleaved text + binary):
1. `{"type":"status","state":"transcribing"}` — UI status update
2. `{"type":"audio_start","sample_rate":24000,"channels":1,"bits":16}` — begin streaming
3. Binary PCM chunks at ~real-time rate — ring buffer → playback
4. `{"type":"token","content":"..."}` — streamed agent text
5. `{"type":"audio_end"}` — end of audio stream
6. `{"type":"done"}` — response complete, show final screen

See [`specs/TechnicalSpec.md`](specs/TechnicalSpec.md) for the full protocol and state machine.

### Audio format

- **Recording:** 16 kHz, 16-bit, stereo PCM, WAV header (max 30 s / 1.92 MB)
- **Playback (streaming):** negotiated via `audio_start` — typically 24 kHz, 16-bit, mono PCM
- **Playback (fallback):** server may also send a complete WAV or raw PCM buffer in a single binary frame without an `audio_start` preamble

### FreeRTOS tasks (all on Core 1)

| Task | Prio | Stack | Role |
|------|------|-------|------|
| `LVGL` | 4 | 8 KB | Render loop, owns LVGL mutex |
| `ws_task` | 3 | 20 KB | WebSocket connect/poll/send, JSON parse, buffer writes |
| `stream_task` | 5 | 8 KB | Streaming PCM playback (spawned on `audio_start`) |
| `state_task` | 3 | 8 KB | State machine — consumes `state_queue` |
| `btn_task` | 3 | 4 KB | Button event translation |
| `touch_task` | 3 | 4 KB | FT6336 touch processing (only if detected) |
| `wifi_task` | 2 | 4 KB | WiFi reconnect loop |
| `bat_task` | 1 | 4 KB | Battery measurement every 30 s |
| `sleep_timer` | 1 | 4 KB | 60 s inactivity countdown |

## Adding a Language

1. Add a `const LangMessages MSG_XX` table in `messages.cpp` with all 15 fields.
2. Declare it `extern` in `messages.h`.
3. Append its pointer to `lang_table[]` in `messages.cpp`.
4. The table count is derived automatically via `sizeof`.

See `MSG_ES` and `MSG_EN` for the reference structure.

## Project Structure

```
ePaperConversational/
├── ePaperConversational.ino       # Entry point (setup/loop)
├── user_app.h / .cpp              # State machine, LVGL, tasks, touch, deep sleep
├── ws_client.h / .cpp             # WebSocket client, JSON parser, audio buffer mgmt
├── audio_stream.h / .cpp          # 512 KB PSRAM ring buffer for streaming
├── audio_bsp.h / .cpp             # ES8311 codec control, recording, playback, beeps
├── messages.h / .cpp              # i18n (ES / EN)
├── wifi_bsp.h / .cpp              # Multi-WiFi scan/connect, LED control
├── user_config.h                  # All non-secret constants
├── user_config_secrets.h          # Gitignored: WiFi & API_BASE_URL
├── lv_conf.h                      # LVGL 9 config — copy to Arduino library
├── font_montserrat_latin_14.c     # Project font
├── src/
│   ├── display/                   # E-paper SPI driver
│   ├── power/                     # GPIO power rail control
│   ├── button_bsp/                # Vendored multi_button
│   ├── i2c_bsp/                   # I2C bus init
│   ├── touch_bsp/                 # FT6336 driver
│   ├── battery/                   # ADC battery measurement
│   ├── codec_board/               # Vendored codec board init
│   ├── esp_codec_dev/             # Vendored esp_codec_dev v1.3.5
│   └── ui/
│       ├── screens.cpp            # Screen creation + status coalescing
│       └── status_bar.cpp         # WiFi + battery status bar
└── specs/
    └── TechnicalSpec.md           # Complete technical specification
```

## Troubleshooting

### Build fails with "Enable PSRAM"
Set **PSRAM: OPI PSRAM** in Arduino IDE board options.

### LVGL version mismatch
Ensure exactly one copy of **lvgl 9.x** is installed. The project enforces `#if LVGL_VERSION_MAJOR != 9` at compile time.

### WebSocket messages truncated
`user_config.h` must be included **before** `<ArduinoWebsockets.h>`. The project already does this in `ws_client.cpp` — do not reorder includes.

### Short beeps are silent
This is by design. All beep waveforms start with 50–70 ms of silence to prime the I2S TX DMA. Do not remove the leading silence.

### Flash budget exceeded
`lv_conf.h` aggressively disables unused LVGL features. If you add libraries or large code, switch to the **Huge APP (3 MB)** partition scheme.

### LED seems inverted
LED on GPIO 3 is active LOW. `wifi_led_write(true)` means **on** (the helper already inverts the polarity). The LED blinks during connection and stays off otherwise.
