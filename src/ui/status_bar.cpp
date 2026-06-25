#include "status_bar.h"

static lv_obj_t* wifi_label = NULL;
static lv_obj_t* battery_label = NULL;

lv_obj_t* create_status_bar(lv_obj_t* parent)
{
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 200, 24);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    wifi_label = lv_label_create(bar);
    lv_label_set_text(wifi_label, "WIFI: OK");
    lv_obj_set_pos(wifi_label, 4, 2);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    battery_label = lv_label_create(bar);
    lv_label_set_text(battery_label, "BAT 100%");
    lv_obj_set_pos(battery_label, 142, 2);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    lv_obj_t* line = lv_line_create(bar);
    static lv_point_t line_pts[] = { {0, 22}, {199, 22} };
    lv_line_set_points(line, line_pts, 2);
    lv_obj_set_style_line_color(line, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(line, 1, LV_STATE_DEFAULT);

    return bar;
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
