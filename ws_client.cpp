#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include "ws_client.h"
#include "user_app.h"
#include "user_config.h"
#include "wifi_bsp.h"
#include "src/ui/screens.h"
#include "messages.h"
#include "esp_heap_caps.h"

extern QueueHandle_t state_queue;

using namespace websockets;

static WebsocketsClient client;
static bool connected = false;
static unsigned long request_start_ms = 0;
static bool waiting_response = false;

static uint8_t* audio_buffer = NULL;
static size_t audio_size = 0;

static char agent_text[1024] = {0};
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
    String haystack = String(str);
    String search = String("\"") + key + "\":\"";
    int idx = haystack.indexOf(search);
    if (idx < 0) return;
    idx += search.length();
    int end = haystack.indexOf("\"", idx);
    if (end < 0) return;
    int len = end - idx;
    if (len <= 0) return;
    if ((size_t)len >= out_max) len = out_max - 1;
    memcpy(out, haystack.c_str() + idx, len);
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
    String s = String(str);
    return s.indexOf(String("\"type\":\"") + type_val + "\"") >= 0;
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
}

static void send_event(AppEvent evt)
{
    xQueueSend(state_queue, &evt, 0);
}

static void onMessageCallback(WebsocketsMessage message)
{
    if (message.isBinary()) {
        size_t len = message.length();
        Serial.printf("WS BIN: %u bytes\n", (unsigned int)len);
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
    Serial.printf("WS TEXT: %.*s\n", (int)length, payload);

    if (parse_json_has_type(payload, "audio_info")) {
    } else if (parse_json_has_type(payload, "status")) {
        const char* state = parse_status_state(payload);
        if (lvgl_lock(200)) {
            if (strcmp(state, "transcribing") == 0) {
                update_screen_receiving_status(currentLang->transcribiendo);
            } else if (strcmp(state, "thinking") == 0) {
                update_screen_receiving_status(currentLang->pensando);
            } else if (strcmp(state, "speaking") == 0) {
                update_screen_receiving_status(currentLang->hablando);
            }
            lvgl_unlock();
        }
    } else if (parse_json_has_type(payload, "token")) {
        parse_json_string(payload, "content", agent_text + agent_text_len, sizeof(agent_text) - agent_text_len);
        agent_text_len = strlen(agent_text);
    } else if (parse_json_has_type(payload, "text")) {
        parse_json_string(payload, "content", agent_text, sizeof(agent_text));
        agent_text_len = strlen(agent_text);
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
