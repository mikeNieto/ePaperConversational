#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "api_client.h"
#include "user_config.h"

#define API_TIMEOUT_SHORT  5000

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
