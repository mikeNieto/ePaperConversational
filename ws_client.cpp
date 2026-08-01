#include <Arduino.h>
#include <WiFi.h>
#include "user_config.h"
#include <ArduinoWebsockets.h>
#include "ws_client.h"
#include "user_app.h"
#include "wifi_bsp.h"
#include "src/ui/screens.h"
#include "messages.h"
#include "esp_heap_caps.h"
#include "audio_stream.h"

extern QueueHandle_t state_queue;

using namespace websockets;

static WebsocketsClient client;
static bool connected = false;
static unsigned long request_start_ms = 0;
static bool waiting_response = false;

static uint8_t* audio_buffer = NULL;
static size_t audio_size = 0;
static size_t audio_offset = 0;
static size_t audio_capacity = 0;
static bool audio_streaming = false;
static int audio_sr = 24000;
static int audio_ch = 1;
static int audio_bps = 16;
static bool audio_is_pcm = false;

static char agent_text[AGENT_TEXT_SIZE] = {0};
static size_t agent_text_len = 0;

typedef enum { WS_CMD_SEND_AUDIO, WS_CMD_RECONNECT } WsCmdType;
typedef struct {
    WsCmdType type;
    const uint8_t* wav_buf;
    uint32_t wav_size;
} WsCmd;

static QueueHandle_t ws_cmd_queue = NULL;

static int json_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool json_parse_hex4(const char* str, uint32_t* value)
{
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        if (str[i] == '\0') return false;
        int digit = json_hex_value(str[i]);
        if (digit < 0) return false;
        result = (result << 4) | (uint32_t)digit;
    }
    *value = result;
    return true;
}

static bool json_append_codepoint(char* out, size_t out_max, size_t* out_len, uint32_t codepoint)
{
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) codepoint = 0xFFFD;
    if (codepoint > 0x10FFFF) codepoint = 0xFFFD;

    uint8_t encoded[4];
    size_t encoded_len;
    if (codepoint <= 0x7F) {
        encoded[0] = (uint8_t)codepoint;
        encoded_len = 1;
    } else if (codepoint <= 0x7FF) {
        encoded[0] = (uint8_t)(0xC0 | (codepoint >> 6));
        encoded[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
        encoded_len = 2;
    } else if (codepoint <= 0xFFFF) {
        encoded[0] = (uint8_t)(0xE0 | (codepoint >> 12));
        encoded[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
        encoded_len = 3;
    } else {
        encoded[0] = (uint8_t)(0xF0 | (codepoint >> 18));
        encoded[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
        encoded_len = 4;
    }

    if (*out_len + encoded_len >= out_max) return false;
    memcpy(out + *out_len, encoded, encoded_len);
    *out_len += encoded_len;
    out[*out_len] = '\0';
    return true;
}

static void parse_json_string(const char* str, const char* key, char* out, size_t out_max)
{
    out[0] = '\0';
    if (!str || !key || out_max == 0) return;

    char search[64];
    int search_len = snprintf(search, sizeof(search), "\"%s\"", key);
    if (search_len < 0 || search_len >= (int)sizeof(search)) return;

    const char* cursor = strstr(str, search);
    if (!cursor) return;
    cursor += search_len;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != ':') return;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != '"') return;
    cursor++;

    size_t out_len = 0;
    while (*cursor && *cursor != '"') {
        uint32_t codepoint;
        if (*cursor != '\\') {
            if (out_len + 1 >= out_max) return;
            out[out_len++] = *cursor++;
            out[out_len] = '\0';
            continue;
        }

        cursor++;
        if (*cursor == '\0') {
            json_append_codepoint(out, out_max, &out_len, '\\');
            return;
        }

        char escaped = *cursor++;
        switch (escaped) {
            case '"': codepoint = '"'; break;
            case '\\': codepoint = '\\'; break;
            case '/': codepoint = '/'; break;
            case 'b': codepoint = '\b'; break;
            case 'f': codepoint = '\f'; break;
            case 'n': codepoint = '\n'; break;
            case 'r': codepoint = '\r'; break;
            case 't': codepoint = '\t'; break;
            case 'u': {
                if (!json_parse_hex4(cursor, &codepoint)) return;
                cursor += 4;
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                    cursor[0] == '\\' && cursor[1] == 'u') {
                    uint32_t low;
                    if (json_parse_hex4(cursor + 2, &low) && low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        cursor += 6;
                    }
                }
                break;
            }
            default:
                /* Preserve malformed escapes instead of silently truncating the text. */
                if (!json_append_codepoint(out, out_max, &out_len, '\\')) return;
                codepoint = (uint8_t)escaped;
                break;
        }
        if (!json_append_codepoint(out, out_max, &out_len, codepoint)) return;
    }
}

static bool parse_json_has_type(const char* str, const char* type_val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"type\":\"%s\"", type_val);
    return strstr(str, search) != NULL;
}

static int parse_json_int(const char* str, const char* key, int default_val)
{
    char search[64];
    int n = snprintf(search, sizeof(search), "\"%s\":", key);
    if (n < 0 || n >= (int)sizeof(search)) return default_val;
    const char* start = strstr(str, search);
    if (!start) return default_val;
    start += n;
    while (*start == ' ' || *start == '\t') start++;
    return atoi(start);
}

static const char* parse_status_state(const char* str)
{
    static char state[32];
    parse_json_string(str, "state", state, sizeof(state));
    return state;
}

static void free_audio_buffer(void)
{
    if (audio_buffer) { free(audio_buffer); audio_buffer = NULL; }
    audio_size = 0;
    audio_offset = 0;
    audio_capacity = 0;
    audio_streaming = false;
    audio_is_pcm = false;
    stream_buf_free();
}

static void send_event(AppEvent evt)
{
    xQueueSend(state_queue, &evt, 0);
}

static void onMessageCallback(WebsocketsMessage message)
{
    if (message.isBinary()) {
        size_t len = message.length();

        if (audio_streaming) {
            if (!stream_buf_write((const uint8_t*)message.c_str(), len, STREAM_TIMEOUT_MS)) {
                Serial.printf("WS: stream buf write failed (full/timeout), chunk lost\n");
            }
            audio_offset += len;
            if (audio_offset % (32 * 1024) < len || audio_offset == len) {
                Serial.printf("WS BIN chunk: %u total=%u\n", (unsigned int)len, (unsigned int)audio_offset);
            }
            return;
        }

        // A new binary message must not free the ring buffer while an old
        // streaming task is still draining it.
        if (audio_wav_is_playing()) {
            audio_play_wav_stop();
        }
        free_audio_buffer();
        audio_buffer = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
        if (audio_buffer) {
            memcpy(audio_buffer, message.c_str(), len);
            audio_size = len;
        } else {
            Serial.printf("WS: failed to alloc %u bytes for audio\n", (unsigned int)len);
        }
        return;
    }

    const char* payload = message.c_str();
    size_t length = message.length();
    bool is_audio_control = parse_json_has_type(payload, "audio_start") ||
                            parse_json_has_type(payload, "audio_end") ||
                            parse_json_has_type(payload, "error");
    if (!audio_streaming || is_audio_control) {
        Serial.printf("WS TEXT: %.*s%s\n", (int)(length > 200 ? 200 : length), payload, length > 200 ? "..." : "");
    }

    if (parse_json_has_type(payload, "audio_start")) {
        if (audio_wav_is_playing()) {
            audio_play_wav_stop();
        }
        free_audio_buffer();
        audio_streaming = true;
        audio_is_pcm = true;
        audio_offset = 0;
        audio_sr = parse_json_int(payload, "sample_rate", 24000);
        audio_ch = parse_json_int(payload, "channels", 1);
        audio_bps = parse_json_int(payload, "bits", 16);
        if (audio_ch < 1 || audio_ch > 2) audio_ch = 1;
        audio_stream_playback_start(audio_sr, audio_ch, audio_bps);
        if (!audio_wav_is_playing()) {
            audio_streaming = false;
        }
        Serial.printf("WS: audio streaming started %dHz %dch %dbps\n", audio_sr, audio_ch, audio_bps);
    } else if (parse_json_has_type(payload, "audio_end")) {
        audio_streaming = false;
        stream_buf_signal_end();
        audio_size = 0;
        Serial.printf("WS: pcm streaming ended, %dHz %dch %dbps\n",
                       audio_sr, audio_ch, audio_bps);
    } else if (parse_json_has_type(payload, "audio_info")) {
        Serial.flush();
    } else if (parse_json_has_type(payload, "status")) {
        const char* state = parse_status_state(payload);
        if (g_app_state == STATE_RECEIVING) {
            if (strcmp(state, "transcribing") == 0) {
                queue_screen_receiving_status(currentLang->transcribing);
            } else if (strcmp(state, "thinking") == 0) {
                queue_screen_receiving_status(currentLang->thinking);
            } else if (strcmp(state, "speaking") == 0) {
                if (agent_text_len > 0) {
                    queue_screen_receiving_status(agent_text);
                } else {
                    queue_screen_receiving_status(currentLang->speaking);
                }
            }
        }
    } else if (parse_json_has_type(payload, "token")) {
        if (agent_text_len < sizeof(agent_text) - 1) {
            parse_json_string(payload, "content", agent_text + agent_text_len, sizeof(agent_text) - agent_text_len);
            agent_text_len = strlen(agent_text);
        }
        if (agent_text_len >= sizeof(agent_text) - 4) {
            Serial.printf("WS: agent_text near full (%u/%u)\n", (unsigned int)agent_text_len, (unsigned int)sizeof(agent_text));
        }
        if (g_app_state == STATE_RECEIVING) {
            queue_screen_receiving_status(agent_text);
        }
    } else if (parse_json_has_type(payload, "text")) {
        parse_json_string(payload, "content", agent_text, sizeof(agent_text));
        agent_text_len = strlen(agent_text);
        if (g_app_state == STATE_RECEIVING) {
            queue_screen_receiving_status(agent_text);
        }
    } else if (parse_json_has_type(payload, "done")) {
        waiting_response = false;
        memcpy(g_agent_text, agent_text, sizeof(g_agent_text));
        Serial.printf("WS: response text length=%u%s\n",
                      (unsigned int)agent_text_len,
                      agent_text_len >= sizeof(agent_text) - 1 ? " (buffer limit)" : "");
        send_event({EVT_RESPONSE_READY, 0});
    } else if (parse_json_has_type(payload, "error")) {
        char err[128];
        parse_json_string(payload, "message", err, sizeof(err));
        Serial.printf("WS error: %s\n", err);
        waiting_response = false;
        send_event({EVT_WS_ERROR, 0});
    }
}

static void onEventCallback(WebsocketsEvent event, String data)
{
    switch (event) {
        case WebsocketsEvent::ConnectionOpened:
            Serial.printf("WS: connected\n");
            connected = true;
            waiting_response = false;
            send_event({EVT_WS_CONNECTED, 0});
            break;

        case WebsocketsEvent::ConnectionClosed:
            Serial.printf("WS: disconnected\n");
            connected = false;
            waiting_response = false;
            send_event({EVT_WS_DISCONNECTED, 0});
            break;

        case WebsocketsEvent::GotPing:
        case WebsocketsEvent::GotPong:
            break;
    }
}

void ws_init(void)
{
    connected = false;
    waiting_response = false;
    agent_text_len = 0;
    agent_text[0] = '\0';
    free_audio_buffer();

    client.onMessage(onMessageCallback);
    client.onEvent(onEventCallback);
    client.setUseMasking(true);

    if (!ws_cmd_queue) {
        ws_cmd_queue = xQueueCreate(4, sizeof(WsCmd));
    }
}

void ws_task(void *arg)
{
    String host;
    int port = 80;
    {
        String url = API_BASE_URL;
        if (url.startsWith("http://")) url = url.substring(7);
        int slash = url.indexOf('/');
        if (slash > 0) { host = url.substring(0, slash); }
        else { host = url; }
        int colon = host.indexOf(':');
        if (colon > 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }
    }

    String wsUrl = "ws://" + host + ":" + String(port) + "/ws";

    WiFi.setSleep(false);
    ws_init();

    for (;;) {
        if (g_app_state == STATE_CONNECTING) {
            static unsigned long last_led_ms = 0;
            static bool ws_led_state = false;
            unsigned long now = millis();
            if (now - last_led_ms >= 200) {
                last_led_ms = now;
                ws_led_state = !ws_led_state;
                wifi_led_write(ws_led_state);
            }
        } else {
            wifi_led_write(false);
        }

        if (!wifi_is_connected()) {
            if (connected) { client.close(); connected = false; }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!connected) {
            Serial.printf("WS: connecting to %s\n", wsUrl.c_str());
            if (client.connect(wsUrl.c_str())) {
            } else {
                Serial.printf("WS: connect failed, retrying in 3s\n");
        client.poll();
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
        }

        WsCmd cmd;
        while (xQueueReceive(ws_cmd_queue, &cmd, 0) == pdTRUE) {
            if (cmd.type == WS_CMD_SEND_AUDIO) {
                if (connected && cmd.wav_buf && cmd.wav_size > 0) {
                    agent_text_len = 0;
                    agent_text[0] = '\0';
                    bool audio_sent = client.sendBinary((const char*)cmd.wav_buf, cmd.wav_size);
                    bool end_sent = client.send("{\"type\":\"audio_end\"}");
                    request_start_ms = millis();
                    waiting_response = true;
                    audio_free_recording_buffer();
                    Serial.printf("WS: sent %u bytes WAV + audio_end (data=%d end=%d)\n",
                                  cmd.wav_size, audio_sent, end_sent);
                }
            } else if (cmd.type == WS_CMD_RECONNECT) {
                client.close();
                connected = false;
            }
        }

        client.poll();

        if (connected && waiting_response && request_start_ms > 0) {
            if (millis() - request_start_ms > WS_REQUEST_TIMEOUT_MS) {
                Serial.printf("WS: request timeout\n");
                waiting_response = false;
                send_event({EVT_WS_ERROR, 0});
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool ws_is_connected(void)
{
    return connected;
}

void ws_send_audio(const uint8_t* wav_buf, uint32_t wav_size)
{
    if (!connected) {
        Serial.printf("WS: not connected, cannot send audio\n");
        audio_free_recording_buffer();
        return;
    }
    WsCmd cmd = { WS_CMD_SEND_AUDIO, wav_buf, wav_size };
    if (xQueueSend(ws_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
        audio_free_recording_buffer();
    }
}

void ws_request_reconnect(void)
{
    if (connected) {
        WsCmd cmd = { WS_CMD_RECONNECT, NULL, 0 };
        xQueueSend(ws_cmd_queue, &cmd, portMAX_DELAY);
    }
    waiting_response = false;
    agent_text_len = 0;
    agent_text[0] = '\0';
    free_audio_buffer();
}

uint8_t* ws_get_audio_buffer(void) { return audio_buffer; }
size_t ws_get_audio_size(void) { return audio_size; }
void ws_free_audio_buffer(void) { free_audio_buffer(); }
bool ws_audio_is_pcm(void) { return audio_is_pcm; }
int ws_audio_get_sample_rate(void) { return audio_sr; }
int ws_audio_get_channels(void) { return audio_ch; }
int ws_audio_get_bits(void) { return audio_bps; }
