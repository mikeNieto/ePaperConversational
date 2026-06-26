#include <Arduino.h>
#include <WiFi.h>
#include "wifi_bsp.h"
#include "esp_log.h"
#include "user_config.h"
#include "user_app.h"
#include "src/ui/status_bar.h"
#include "api_client.h"

static const char *TAG_WIFI = "wifi";

EventGroupHandle_t wifi_event_group = NULL;

void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    xEventGroupSetBits(wifi_event_group, WIFI_BIT_DISCONNECTED);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("WiFi: connecting to %s (async)...\n", WIFI_SSID);
}

bool wifi_is_connected(void)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_BIT_CONNECTED) != 0;
}

void wifi_task(void *arg)
{
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            EventBits_t bits = xEventGroupGetBits(wifi_event_group);
            if (!(bits & WIFI_BIT_CONNECTED)) {
                xEventGroupSetBits(wifi_event_group, WIFI_BIT_CONNECTED);
                xEventGroupClearBits(wifi_event_group, WIFI_BIT_DISCONNECTED);
                Serial.printf("WiFi reconnected. IP: %s\n", WiFi.localIP().toString().c_str());
                bool api_ok = api_health_check();
                Serial.printf("API health: %s\n", api_ok ? "OK" : "FAIL");
            }
        } else {
            EventBits_t bits = xEventGroupGetBits(wifi_event_group);
            if (bits & WIFI_BIT_CONNECTED) {
                xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONNECTED);
                xEventGroupSetBits(wifi_event_group, WIFI_BIT_DISCONNECTED);
                Serial.printf("WiFi disconnected.\n");
            }
            WiFi.reconnect();
        }

        if (lvgl_lock(100)) {
            status_bar_update_wifi(wifi_is_connected());
            lvgl_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
