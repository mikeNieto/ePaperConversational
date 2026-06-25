#include "status_bar.h"

static lv_obj_t* wifi_label = NULL;
static lv_obj_t* battery_label = NULL;
static lv_obj_t* top_bar = NULL;

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
    lv_label_set_text(wifi_label, "WIFI: --");
    lv_obj_set_pos(wifi_label, 4, 2);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    battery_label = lv_label_create(top_bar);
    lv_label_set_text(battery_label, "BAT 100%");
    lv_obj_set_pos(battery_label, 142, 2);
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
    if (wifi_label) {
        lv_label_set_text(wifi_label, wifiOk ? "WIFI: OK" : "WIFI: --");
    }
    if (battery_label) {
        lv_label_set_text_fmt(battery_label, "BAT %d%%", batteryPct);
    }
}

void status_bar_update_wifi(bool wifiOk)
{
    if (wifi_label) {
        lv_label_set_text(wifi_label, wifiOk ? "WIFI: OK" : "WIFI: --");
    }
}
