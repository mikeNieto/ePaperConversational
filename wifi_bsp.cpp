#include <Arduino.h>
#include <WiFi.h>
#include "wifi_bsp.h"
#include "esp_log.h"
#include "user_config.h"
#include "user_app.h"
#include "src/ui/status_bar.h"

static const char *TAG_WIFI = "wifi";

EventGroupHandle_t wifi_event_group = NULL;

void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    xEventGroupSetBits(wifi_event_group, WIFI_BIT_DISCONNECTED);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    ESP_LOGI(TAG_WIFI, "Connecting to %s...", WIFI_SSID);
    Serial.printf("WiFi: connecting to %s\n", WIFI_SSID);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        xEventGroupSetBits(wifi_event_group, WIFI_BIT_CONNECTED);
        xEventGroupClearBits(wifi_event_group, WIFI_BIT_DISCONNECTED);
        ESP_LOGI(TAG_WIFI, "Connected. IP: %s", WiFi.localIP().toString().c_str());
        Serial.printf("WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        ESP_LOGW(TAG_WIFI, "Connection timeout");
        Serial.printf("WiFi timeout. Will retry.\n");
    }
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
