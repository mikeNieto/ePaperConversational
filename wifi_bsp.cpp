#include <Arduino.h>
#include <WiFi.h>
#include "wifi_bsp.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "user_config.h"
#include "user_app.h"
#include "src/ui/status_bar.h"

static const char *TAG_WIFI = "wifi";
static bool led_state = false;

#define LED_PIN GPIO_NUM_3

static void led_init(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << LED_PIN);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(LED_PIN, 0);
}

static void led_set(bool on)
{
    gpio_set_level(LED_PIN, on ? 1 : 0);
}

void wifi_led_write(bool on)
{
    led_set(on);
}

EventGroupHandle_t wifi_event_group = NULL;

void wifi_init(void)
{
    led_init();
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
                Serial.printf("WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
            }
        } else {
            led_state = !led_state;
            led_set(led_state);
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
