#include <Arduino.h>
#include <WiFi.h>
#include "wifi_bsp.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "user_config.h"
#include "user_app.h"
#include "src/ui/status_bar.h"

static const char *TAG_WIFI = "wifi";
#define LED_PIN GPIO_NUM_3

struct WifiCredential {
    const char* ssid;
    const char* password;
};

static const WifiCredential wifi_list[] = { WIFI_NETWORKS };

static void led_init(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << LED_PIN);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(LED_PIN, 1);
}

static void led_set(bool on)
{
    gpio_set_level(LED_PIN, on ? 0 : 1);
}

void wifi_led_write(bool on)
{
    led_set(on);
}

const char* wifi_get_ssid(void)
{
    static String ssid_buf;
    if (WiFi.status() == WL_CONNECTED) {
        ssid_buf = WiFi.SSID();
    } else {
        ssid_buf = "--";
    }
    return ssid_buf.c_str();
}

EventGroupHandle_t wifi_event_group = NULL;

void wifi_connect_best(void)
{
    if (WiFi.status() == WL_CONNECTED) return;

    int n = WiFi.scanNetworks();
    if (n < 0) {
        ESP_LOGW(TAG_WIFI, "Scan failed: %d", n);
        return;
    }

    int best_idx = -1;
    int best_rssi = -999;
    const char* best_pass = NULL;

    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);

        for (int j = 0; j < WIFI_NETWORK_COUNT; j++) {
            if (ssid == wifi_list[j].ssid) {
                if (rssi > best_rssi) {
                    best_rssi = rssi;
                    best_idx = i;
                    best_pass = wifi_list[j].password;
                }
                break;
            }
        }
    }

    if (best_idx < 0) {
        ESP_LOGW(TAG_WIFI, "No known network found (%d scanned)", n);
        WiFi.scanDelete();
        return;
    }

    String best_ssid = WiFi.SSID(best_idx);
    Serial.printf("WiFi: connecting to %s (RSSI: %d dBm)...\n", best_ssid.c_str(), best_rssi);

    WiFi.scanDelete();
    WiFi.begin(best_ssid.c_str(), best_pass);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi: connected to %s  IP: %s\n", best_ssid.c_str(), WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("WiFi: failed to connect to %s\n", best_ssid.c_str());
    }
}

void wifi_init(void)
{
    led_init();
    wifi_event_group = xEventGroupCreate();
    xEventGroupSetBits(wifi_event_group, WIFI_BIT_DISCONNECTED);

    WiFi.mode(WIFI_STA);
    wifi_connect_best();
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
                Serial.printf("WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
            }
        } else {
            EventBits_t bits = xEventGroupGetBits(wifi_event_group);
            if (bits & WIFI_BIT_CONNECTED) {
                xEventGroupClearBits(wifi_event_group, WIFI_BIT_CONNECTED);
                xEventGroupSetBits(wifi_event_group, WIFI_BIT_DISCONNECTED);
                Serial.printf("WiFi disconnected.\n");
            }
            wifi_connect_best();
        }

        if (lvgl_lock(100)) {
            status_bar_update_wifi(wifi_is_connected());
            lvgl_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
