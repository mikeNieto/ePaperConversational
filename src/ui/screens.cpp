#include "screens.h"
#include "status_bar.h"
#include "messages.h"

#define STATUS_BAR_H 24

static void center_label(lv_obj_t* parent, const char* text)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
}

lv_obj_t* create_screen_0_deep_sleep(int sleep_counter)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_text_fmt(label, "%s %d", currentLang->durmiendo, sleep_counter);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    return screen;
}

lv_obj_t* create_screen_connecting(void)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->conectando);

    return screen;
}

lv_obj_t* create_screen_2_record(void)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->grabar_mensaje);

    return screen;
}

lv_obj_t* create_screen_2b_listening(void)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->escuchando);

    return screen;
}

/* Receiving screen — dynamically updatable label */
static lv_obj_t* receiving_label = NULL;

lv_obj_t* create_screen_receiving(void)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    receiving_label = lv_label_create(screen);
    lv_label_set_text(receiving_label, currentLang->transcribiendo);
    lv_obj_center(receiving_label);
    lv_obj_set_style_text_font(receiving_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    return screen;
}

void update_screen_receiving_status(const char* status)
{
    if (receiving_label) {
        lv_label_set_text(receiving_label, status);
    }
}

lv_obj_t* create_screen_6_response(const char* agentText)
{
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
