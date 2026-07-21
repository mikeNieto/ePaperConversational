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

static void parse_json_string(const char* str, const char* key, char* out, size_t out_max)
{
    out[0] = '\0';
    if (out_max == 0) return;

    char search[64];
    int search_len = snprintf(search, sizeof(search), "\"%s\":\"", key);
    if (search_len < 0 || search_len >= (int)sizeof(search)) return;

    const char* start = strstr(str, search);
    if (!start) return;
    start += search_len;

    const char* end = strchr(start, '"');
    if (!end) return;

    size_t len = (size_t)(end - start);
    if (len == 0) return;
    if (len >= out_max) len = out_max - 1;
    memcpy(out, start, len);
    out[len] = '\0';

    for (int i = 0; out[i]; i++) {
        if (out[i] == '\\') {
            if (out[i + 1] == 'n') { out[i] = '\n'; memmove(out + i + 1, out + i + 2, strlen(out + i + 2) + 1); }
            else if (out[i + 1] == 't') { out[i] = '\t'; memmove(out + i + 1, out + i + 2, strlen(out + i + 2) + 1); }
            else if (out[i + 1] == '"') { out[i] = '"'; memmove(out + i + 1, out + i + 2, strlen(out + i + 2) + 1); }
        }
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
            if (!stream_buf_write((const uint8_t*)message.c_str(), len, 5000)) {
                Serial.printf("WS: stream buf write failed (full/timeout), chunk lost\n");
            }
            audio_offset += len;
            if (audio_offset % (32 * 1024) < len || audio_offset == len) {
                Serial.printf("WS BIN chunk: %u total=%u\n", (unsigned int)len, (unsigned int)audio_offset);
            }
            return;
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
    Serial.printf("WS TEXT: %.*s%s\n", (int)(length > 200 ? 200 : length), payload, length > 200 ? "..." : "");

    if (parse_json_has_type(payload, "audio_start")) {
        free_audio_buffer();
        audio_streaming = true;
        audio_is_pcm = true;
        audio_offset = 0;
        audio_sr = parse_json_int(payload, "sample_rate", 24000);
        audio_ch = parse_json_int(payload, "channels", 1);
        audio_bps = parse_json_int(payload, "bits", 16);
        if (audio_ch < 1 || audio_ch > 2) audio_ch = 1;
        audio_stream_playback_start(audio_sr, audio_ch, audio_bps);
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
        if (lvgl_lock(200)) {
            if (strcmp(state, "transcribing") == 0) {
                update_screen_receiving_status(currentLang->transcribiendo);
            } else if (strcmp(state, "thinking") == 0) {
                update_screen_receiving_status(currentLang->pensando);
            } else if (strcmp(state, "speaking") == 0) {
                if (agent_text_len > 0) {
                    update_screen_receiving_status(agent_text);
                } else {
                    update_screen_receiving_status(currentLang->hablando);
                }
            }
            lvgl_unlock();
        }
    } else if (parse_json_has_type(payload, "token")) {
        if (agent_text_len < sizeof(agent_text) - 1) {
            parse_json_string(payload, "content", agent_text + agent_text_len, sizeof(agent_text) - agent_text_len);
            agent_text_len = strlen(agent_text);
        }
        if (agent_text_len >= sizeof(agent_text) - 4) {
            Serial.printf("WS: agent_text near full (%u/%u)\n", (unsigned int)agent_text_len, (unsigned int)sizeof(agent_text));
        }
        if (g_app_state == STATE_RECEIVING && lvgl_lock(200)) {
            update_screen_receiving_status(agent_text);
            lvgl_unlock();
        }
    } else if (parse_json_has_type(payload, "text")) {
        parse_json_string(payload, "content", agent_text, sizeof(agent_text));
        agent_text_len = strlen(agent_text);
        if (g_app_state == STATE_RECEIVING && lvgl_lock(200)) {
            update_screen_receiving_status(agent_text);
            lvgl_unlock();
        }
    } else if (parse_json_has_type(payload, "done")) {
        waiting_response = false;
        memcpy(g_agent_text, agent_text, sizeof(g_agent_text));
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
                    client.sendBinary((const char*)cmd.wav_buf, cmd.wav_size);
                    client.send("{\"type\":\"audio_end\"}");
                    request_start_ms = millis();
                    waiting_response = true;
                    Serial.printf("WS: sent %u bytes WAV + audio_end\n", cmd.wav_size);
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
        return;
    }
    WsCmd cmd = { WS_CMD_SEND_AUDIO, wav_buf, wav_size };
    xQueueSend(ws_cmd_queue, &cmd, portMAX_DELAY);
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
