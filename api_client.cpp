#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "api_client.h"
#include "user_config.h"
#include "esp_heap_caps.h"

#define API_TIMEOUT_SHORT  5000
#define API_TIMEOUT_LONG  120000

static String build_url(const char* path) {
    String url = API_BASE_URL;
    if (path[0] == '/') url += path;
    else { url += "/"; url += path; }
    return url;
}

bool api_health_check(void)
{
    HTTPClient http;
    http.begin(build_url("/api/health"));
    http.setTimeout(API_TIMEOUT_SHORT);
    int code = http.GET();
    http.end();
    return (code == 200);
}

bool api_transcribe_audio(uint8_t* wav_buffer, size_t wav_size,
                           char* transcribed_text, size_t text_maxlen)
{
    String boundary = "----ESP32Boundary";
    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    bodyStart += "Content-Type: audio/wav\r\n\r\n";
    String bodyEnd = "\r\n--" + boundary + "--\r\n";

    size_t totalSize = bodyStart.length() + wav_size + bodyEnd.length();
    uint8_t* postData = (uint8_t*)malloc(totalSize);
    if (!postData) return false;

    memcpy(postData, bodyStart.c_str(), bodyStart.length());
    memcpy(postData + bodyStart.length(), wav_buffer, wav_size);
    memcpy(postData + bodyStart.length() + wav_size, bodyEnd.c_str(), bodyEnd.length());

    HTTPClient http;
    http.begin(build_url("/api/audio/transcribe"));
    http.setTimeout(API_TIMEOUT_LONG);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    int code = http.POST(postData, totalSize);
    free(postData);

    bool ok = false;
    if (code == 200) {
        String response = http.getString();
        int idx = response.indexOf("\"original_text\":\"");
        if (idx > 0) {
            idx += 17;
            int end = response.indexOf("\"", idx);
            if (end > idx) {
                String txt = response.substring(idx, end);
                snprintf(transcribed_text, text_maxlen, "%s", txt.c_str());
                ok = true;
            }
        }
        if (!ok) snprintf(transcribed_text, text_maxlen, "%s", response.c_str());
    }
    http.end();
    json_unescape_utf8(transcribed_text);
    return (code == 200);
}

bool api_send_message(const char* thread_id, const char* message,
                       char* agent_text, size_t text_maxlen,
                       char* audio_url, size_t url_maxlen)
{
    String json = "{\"user_id\":\"" + String(USER_ID) + "\",";
    json += "\"thread_id\":\"" + String(thread_id) + "\",";
    json += "\"message\":\"" + String(message) + "\",";
    json += "\"response_audio\":true}";

    HTTPClient http;
    http.begin(build_url("/api/chat/message"));
    http.setTimeout(API_TIMEOUT_LONG);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(json);
    bool ok = false;

    if (code == 200) {
        String response = http.getString();

        int idx = response.indexOf("\"agent_text\":\"");
        if (idx > 0) {
            idx += 15;
            int end = response.indexOf("\",", idx);
            if (end < 0) end = response.indexOf("\"}", idx);
            if (end > idx) {
                snprintf(agent_text, text_maxlen, "%s", response.substring(idx, end).c_str());
            }
        }

        idx = response.indexOf("\"audio_url\":\"");
        if (idx > 0) {
            idx += 14;
            int end = response.indexOf("\"", idx);
            if (end > idx) {
                snprintf(audio_url, url_maxlen, "%s", response.substring(idx, end).c_str());
            }
        }
        ok = true;
    }
    http.end();
    json_unescape_utf8(agent_text);
    return (code == 200);
}

bool api_download_audio(const char* audio_url, uint8_t** mp3_buffer, size_t* mp3_size)
{
    HTTPClient http;
    http.begin(build_url(audio_url));
    http.setTimeout(API_TIMEOUT_LONG);

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0) len = 256 * 1024;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    while (http.connected() && total < (size_t)len) {
        int available = stream->available();
        if (available > 0) {
            int toRead = available;
            if (total + toRead > (size_t)len) toRead = len - total;
            int r = stream->readBytes(buf + total, toRead);
            if (r > 0) total += r;
        }
        delay(1);
    }

    http.end();
    *mp3_buffer = buf;
    *mp3_size = total;
    return (total > 0);
}

static int hex_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void json_unescape_utf8(char* str)
{
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '\\' && *(src + 1) == 'u') {
            uint16_t cp = (hex_to_int(src[2]) << 12) |
                          (hex_to_int(src[3]) << 8)  |
                          (hex_to_int(src[4]) << 4)  |
                          hex_to_int(src[5]);
            if (cp < 0x80) {
                *dst++ = (char)cp;
            } else if (cp < 0x800) {
                *dst++ = (char)(0xC0 | (cp >> 6));
                *dst++ = (char)(0x80 | (cp & 0x3F));
            } else {
                *dst++ = (char)(0xE0 | (cp >> 12));
                *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                *dst++ = (char)(0x80 | (cp & 0x3F));
            }
            src += 6;
        } else if (*src == '\\') {
            src++;
            switch (*src) {
                case 'n': *dst++ = '\n'; break;
                case 't': *dst++ = '\t'; break;
                case 'r': *dst++ = '\r'; break;
                case '"': *dst++ = '"'; break;
                case '\\': *dst++ = '\\'; break;
                default: *dst++ = *src; break;
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}
