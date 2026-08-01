#include "screens.h"
#include "status_bar.h"
#include "messages.h"
#include "user_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define STATUS_BAR_H 24

/* Text updates are coalesced here so the WebSocket task never waits for an e-paper refresh. */
static lv_obj_t* receiving_label = NULL;
static lv_timer_t* receiving_status_timer = NULL;
static SemaphoreHandle_t receiving_status_mutex = NULL;
static volatile bool receiving_status_ready = false;
static bool receiving_status_pending = false;
static char pending_receiving_status[AGENT_TEXT_SIZE] = {0};

static void receiving_status_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!receiving_status_ready || !receiving_label ||
        lv_scr_act() != lv_obj_get_screen(receiving_label) ||
        !receiving_status_mutex) {
        return;
    }

    if (xSemaphoreTake(receiving_status_mutex, 0) != pdTRUE) return;
    bool has_pending = receiving_status_pending;
    if (has_pending) {
        lv_label_set_text(receiving_label, pending_receiving_status);
        receiving_status_pending = false;
    }
    xSemaphoreGive(receiving_status_mutex);
}

static void receiving_screen_invalidate(void)
{
    receiving_status_ready = false;
    receiving_label = NULL;
}

static void receiving_status_clear_pending(void)
{
    if (!receiving_status_mutex) {
        receiving_status_pending = false;
        pending_receiving_status[0] = '\0';
        return;
    }

    if (xSemaphoreTake(receiving_status_mutex, portMAX_DELAY) == pdTRUE) {
        receiving_status_pending = false;
        pending_receiving_status[0] = '\0';
        xSemaphoreGive(receiving_status_mutex);
    }
}

static void center_label(lv_obj_t* parent, const char* text)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
}

lv_obj_t* create_screen_0_deep_sleep(int sleep_counter)
{
    receiving_screen_invalidate();
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_text_fmt(label, "%s %d", currentLang->sleeping, sleep_counter);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    return screen;
}

lv_obj_t* create_screen_connecting(void)
{
    receiving_screen_invalidate();
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->connecting);

    return screen;
}

lv_obj_t* create_screen_2_record(void)
{
    receiving_screen_invalidate();
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->record_message);

    return screen;
}

lv_obj_t* create_screen_2b_listening(void)
{
    receiving_screen_invalidate();
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->listening);

    return screen;
}

lv_obj_t* create_screen_settings(const char* ssid, const char* lang_name)
{
    receiving_screen_invalidate();
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, currentLang->settings);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_pos(title, 0, STATUS_BAR_H + 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    lv_obj_t* wifi = lv_label_create(screen);
    lv_label_set_text_fmt(wifi, "%s: %s", currentLang->wifi_label, ssid);
    lv_obj_set_pos(wifi, 10, STATUS_BAR_H + 42);
    lv_obj_set_style_text_font(wifi, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    lv_obj_t* lang = lv_label_create(screen);
    lv_label_set_text_fmt(lang, "%s: %s", currentLang->language, lang_name);
    lv_obj_set_pos(lang, 10, STATUS_BAR_H + 64);
    lv_obj_set_style_text_font(lang, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    return screen;
}

lv_obj_t* create_screen_receiving(void)
{
    receiving_screen_invalidate();
    if (!receiving_status_mutex) {
        receiving_status_mutex = xSemaphoreCreateMutex();
    }

    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    lv_obj_t* cont = lv_obj_create(screen);
    lv_obj_set_size(cont, 180, 170);
    lv_obj_set_pos(cont, 10, STATUS_BAR_H + 4);
    lv_obj_set_style_border_width(cont, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 4, LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    receiving_label = lv_label_create(cont);
    lv_label_set_long_mode(receiving_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(receiving_label, lv_pct(100));
    lv_obj_set_style_text_align(receiving_label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_text(receiving_label, currentLang->transcribing);
    lv_obj_set_style_text_font(receiving_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    receiving_status_clear_pending();
    if (!receiving_status_timer) {
        receiving_status_timer = lv_timer_create(receiving_status_timer_cb, 50, NULL);
    }
    receiving_status_ready = receiving_status_mutex != NULL;

    return screen;
}

void update_screen_receiving_status(const char* status)
{
    if (receiving_label) {
        lv_label_set_text(receiving_label, status);
    }
}

void queue_screen_receiving_status(const char* status)
{
    if (!status || !receiving_status_ready || !receiving_status_mutex) return;
    if (xSemaphoreTake(receiving_status_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    strncpy(pending_receiving_status, status, sizeof(pending_receiving_status) - 1);
    pending_receiving_status[sizeof(pending_receiving_status) - 1] = '\0';
    receiving_status_pending = true;
    xSemaphoreGive(receiving_status_mutex);
}

lv_obj_t* create_screen_6_response(const char* agentText)
{
    receiving_screen_invalidate();
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    lv_obj_t* cont = lv_obj_create(screen);
    lv_obj_set_size(cont, 180, 170);
    lv_obj_set_pos(cont, 10, STATUS_BAR_H + 4);
    lv_obj_set_style_border_width(cont, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 4, LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* label = lv_label_create(cont);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_text(label, agentText);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    return screen;
}
