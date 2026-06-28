#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "api_client.h"
#include "user_config.h"
#include "esp_heap_caps.h"

#define API_TIMEOUT_SHORT  5000
#define API_TCP_TIMEOUT    120

static String build_url(const char* path) {
    String url = API_BASE_URL;
    if (path[0] == '/') url += path;
    else { url += "/"; url += path; }
    return url;
}

static String build_host() {
    String url = API_BASE_URL;
    url.replace("http://", "");
    return url;
}

static bool tcp_request(const char* method, const char* path,
                         const char* content_type, const uint8_t* body, size_t body_len,
                         String* response, int* http_code)
{
    String full = build_url(path);
    String host;
    int port = 80;
    String url_path;
    if (full.startsWith("http://")) {
        full = full.substring(7);
        int slash = full.indexOf('/');
        if (slash > 0) { url_path = full.substring(slash); host = full.substring(0, slash); }
        else { url_path = "/"; host = full; }
        int colon = host.indexOf(':');
        if (colon > 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }
    }

    WiFiClient client;
    client.setTimeout(API_TCP_TIMEOUT);

    if (!client.connect(host.c_str(), port)) {
        Serial.printf("TCP connect failed\n");
        return false;
    }

    client.printf("%s %s HTTP/1.1\r\n", method, url_path.c_str());
    client.printf("Host: %s\r\n", host.c_str());
    if (content_type) client.printf("Content-Type: %s\r\n", content_type);
    if (body && body_len > 0) client.printf("Content-Length: %u\r\n", body_len);
    client.print("Connection: close\r\n\r\n");
    client.flush();

    if (body && body_len > 0) {
        size_t written = 0;
        while (written < body_len) {
            int w = client.write(body + written, body_len - written);
            if (w <= 0) { client.stop(); return false; }
            written += w;
        }
        client.flush();
    }

    unsigned long start = millis();
    while (!client.available() && client.connected() && (millis() - start) < 120000) {
        delay(10);
    }

    String line = client.readStringUntil('\n');
    line.trim();
    Serial.printf("HTTP line: %s\n", line.c_str());
    if (http_code) {
        int sp = line.indexOf(' ');
        if (sp > 0) *http_code = line.substring(sp + 1, sp + 4).toInt();
    }

    while (client.connected()) {
        String l = client.readStringUntil('\n');
        if (l == "\r" || l.length() <= 1) break;
    }

    if (response) {
        *response = "";
        while (client.available() || client.connected()) {
            if (client.available()) {
                *response += client.readString();
            }
            delay(1);
        }
    }

    client.stop();
    return true;
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

    String ct = "multipart/form-data; boundary=" + boundary;
    String resp;
    int code = 0;
    bool ok = tcp_request("POST", "/api/audio/transcribe", ct.c_str(), postData, totalSize, &resp, &code);
    free(postData);

    if (ok && code == 200 && resp.length() > 0) {
        int idx = resp.indexOf("\"original_text\":\"");
        if (idx > 0) {
            idx += 17;
            int end = resp.indexOf("\"", idx);
            if (end > idx) {
                snprintf(transcribed_text, text_maxlen, "%s", resp.substring(idx, end).c_str());
                json_unescape_utf8(transcribed_text);
                return true;
            }
        }
        snprintf(transcribed_text, text_maxlen, "%s", resp.c_str());
        return true;
    }
    if (!ok) Serial.printf("Transcribe: TCP error\n");
    else Serial.printf("Transcribe HTTP %d\n", code);
    return false;
}

bool api_send_message(const char* thread_id, const char* message,
                       char* agent_text, size_t text_maxlen,
                       char* audio_url, size_t url_maxlen)
{
    String json = "{\"user_id\":\"" + String(USER_ID) + "\",";
    json += "\"thread_id\":\"" + String(thread_id) + "\",";
    json += "\"message\":\"" + String(message) + "\",";
    json += "\"response_audio\":true}";

    String resp;
    int code = 0;
    if (!tcp_request("POST", "/api/chat/message", "application/json",
                      (const uint8_t*)json.c_str(), json.length(), &resp, &code)) {
        return false;
    }

    if (code == 200 && resp.length() > 0) {
        int idx = resp.indexOf("\"agent_text\":\"");
        if (idx > 0) {
            idx += 14;
            int end = resp.indexOf("\",", idx);
            if (end < 0) end = resp.indexOf("\"}", idx);
            if (end > idx)
                snprintf(agent_text, text_maxlen, "%s", resp.substring(idx, end).c_str());
        }
        idx = resp.indexOf("\"audio_url\":\"");
        if (idx > 0) {
            idx += 14;
            int end = resp.indexOf("\"", idx);
            if (end > idx)
                snprintf(audio_url, url_maxlen, "%s", resp.substring(idx, end).c_str());
        }
        json_unescape_utf8(agent_text);
        return true;
    }
    return false;
}

bool api_download_audio(const char* audio_url, uint8_t** mp3_buffer, size_t* mp3_size)
{
    String full = build_url(audio_url);
    String host;
    int port = 80;
    String path;
    if (full.startsWith("http://")) {
        full = full.substring(7);
        int slash = full.indexOf('/');
        if (slash > 0) { path = full.substring(slash); host = full.substring(0, slash); }
        else { path = "/"; host = full; }
        int colon = host.indexOf(':');
        if (colon > 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }
    } else {
        path = full;
        host = build_host();
        port = 8000;
    }

    WiFiClient client;
    client.setTimeout(API_TCP_TIMEOUT);

    if (!client.connect(host.c_str(), port)) return false;

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path.c_str(), host.c_str());
    client.flush();

    String line = client.readStringUntil('\n');
    int sp = line.indexOf(' ');
    int code = (sp > 0) ? line.substring(sp + 1, sp + 4).toInt() : 0;
    if (code != 200) { client.stop(); return false; }

    int len = 0;
    while (client.connected()) {
        String l = client.readStringUntil('\n');
        if (l.startsWith("Content-Length:")) len = l.substring(15).toInt();
        if (l == "\r" || l.length() <= 1) break;
    }
    if (len <= 0) len = 256 * 1024;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) { client.stop(); return false; }

    size_t total = 0;
    while (client.connected() && total < (size_t)len) {
        int available = client.available();
        if (available > 0) {
            int toRead = available;
            if (total + toRead > (size_t)len) toRead = len - total;
            int r = client.readBytes(buf + total, toRead);
            if (r > 0) total += r;
        }
        delay(1);
    }

    client.stop();
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
