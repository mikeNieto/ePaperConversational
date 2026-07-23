#include "user_app.h"
#include "esp_sleep.h"
#include "src/ui/status_bar.h"
#include "esp_heap_caps.h"
#include "messages.h"

extern RTC_DATA_ATTR int boot_count;
extern RTC_DATA_ATTR int sleep_counter;

void setup()
{
    heap_caps_malloc_extmem_enable(256);
    lang_init();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (boot_count == 0) {
        sleep_counter = 0;
        user_app_init();
        lvgl_port_init();
        lvgl_lock(-1);
        status_bar_set_visible(true);
        user_ui_init();
        lvgl_unlock();
        if (g_app_state != STATE_CONNECTING) switch_state(g_app_state);
    } else if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        sleep_counter = 0;
        g_play_wake_beep = true;
        user_app_init();
        lvgl_port_init();
        lvgl_lock(-1);
        status_bar_set_visible(true);
        user_ui_init();
        lvgl_unlock();
        if (g_app_state != STATE_CONNECTING) switch_state(g_app_state);
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        sleep_counter++;
        display_light_init();
        lvgl_port_init();
        if (lvgl_lock(-1)) {
            status_bar_set_visible(false);
            lv_obj_t* screen0 = create_screen_0_deep_sleep(sleep_counter);
            lv_scr_load(screen0);
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(800));
        enter_deep_sleep_light();
    } else {
        g_play_wake_beep = true;
        user_app_init();
        lvgl_port_init();
        lvgl_lock(-1);
        status_bar_set_visible(true);
        user_ui_init();
        lvgl_unlock();
        if (g_app_state != STATE_CONNECTING) switch_state(g_app_state);
    }

    boot_count++;
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
