#include "status_bar.h"
#include "messages.h"

static lv_obj_t* wifi_label = NULL;
static lv_obj_t* battery_label = NULL;
static lv_obj_t* bar_fill1 = NULL;
static lv_obj_t* bar_fill2 = NULL;
static lv_obj_t* bar_fill3 = NULL;
static lv_obj_t* bar_fill4 = NULL;
static lv_obj_t* top_bar = NULL;

static void draw_battery_icon(lv_obj_t* parent)
{
    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_set_size(body, 20, 11);
    lv_obj_set_pos(body, 119, 4);
    lv_obj_set_style_border_width(body, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(body, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(body, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(body, 0, LV_STATE_DEFAULT);

    lv_obj_t* tip = lv_obj_create(parent);
    lv_obj_set_size(tip, 3, 5);
    lv_obj_set_pos(tip, 139, 7);
    lv_obj_set_style_border_width(tip, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(tip, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(tip, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(tip, 0, LV_STATE_DEFAULT);

    bar_fill1 = lv_obj_create(parent);
    lv_obj_set_size(bar_fill1, 3, 7);
    lv_obj_set_pos(bar_fill1, 122, 6);
    lv_obj_set_style_border_width(bar_fill1, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar_fill1, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar_fill1, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar_fill1, 0, LV_STATE_DEFAULT);

    bar_fill2 = lv_obj_create(parent);
    lv_obj_set_size(bar_fill2, 3, 7);
    lv_obj_set_pos(bar_fill2, 126, 6);
    lv_obj_set_style_border_width(bar_fill2, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar_fill2, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar_fill2, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar_fill2, 0, LV_STATE_DEFAULT);

    bar_fill3 = lv_obj_create(parent);
    lv_obj_set_size(bar_fill3, 3, 7);
    lv_obj_set_pos(bar_fill3, 130, 6);
    lv_obj_set_style_border_width(bar_fill3, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar_fill3, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar_fill3, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar_fill3, 0, LV_STATE_DEFAULT);

    bar_fill4 = lv_obj_create(parent);
    lv_obj_set_size(bar_fill4, 3, 7);
    lv_obj_set_pos(bar_fill4, 134, 6);
    lv_obj_set_style_border_width(bar_fill4, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar_fill4, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar_fill4, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar_fill4, 0, LV_STATE_DEFAULT);
}

lv_obj_t* create_status_bar(lv_obj_t* parent)
{
    if (top_bar) return top_bar;

    top_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(top_bar, 200, 24);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_border_width(top_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(top_bar, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(top_bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    wifi_label = lv_label_create(top_bar);
    lv_label_set_text(wifi_label, currentLang->wifi_off);
    lv_obj_set_pos(wifi_label, 4, 2);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    draw_battery_icon(top_bar);

    battery_label = lv_label_create(top_bar);
    lv_label_set_text(battery_label, "100%");
    lv_obj_set_pos(battery_label, 148, 2);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    lv_obj_t* line = lv_line_create(top_bar);
    static lv_point_t line_pts[] = { {0, 22}, {199, 22} };
    lv_line_set_points(line, line_pts, 2);
    lv_obj_set_style_line_color(line, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(line, 1, LV_STATE_DEFAULT);

    return top_bar;
}

void update_status_bar(lv_obj_t* bar, bool wifiOk, int batteryPct)
{
    if (wifi_label) lv_label_set_text(wifi_label, wifiOk ? currentLang->wifi_ok : currentLang->wifi_off);
    if (battery_label) lv_label_set_text_fmt(battery_label, "%d%%", batteryPct);
    int bars = batteryPct / 25;
    if (bar_fill1) lv_obj_set_style_bg_color(bar_fill1, bars >= 1 ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
    if (bar_fill2) lv_obj_set_style_bg_color(bar_fill2, bars >= 2 ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
    if (bar_fill3) lv_obj_set_style_bg_color(bar_fill3, bars >= 3 ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
    if (bar_fill4) lv_obj_set_style_bg_color(bar_fill4, bars >= 4 ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
}

void status_bar_update_wifi(bool wifiOk)
{
    if (wifi_label) lv_label_set_text(wifi_label, wifiOk ? currentLang->wifi_ok : currentLang->wifi_off);
}

void status_bar_set_visible(bool visible)
{
    if (top_bar) {
        if (visible) lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
    }
}
