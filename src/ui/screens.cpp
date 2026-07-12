#include "screens.h"
#include "status_bar.h"
#include "user_app.h"
#include "messages.h"

extern QueueHandle_t state_queue;
extern void highlight_selection(void);

#define STATUS_BAR_H 24

static void center_label(lv_obj_t* parent, const char* text)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
}

static void on_btn_continuar(lv_event_t* e)
{
    AppEvent evt = { 2, 0 };
    xQueueSend(state_queue, &evt, 0);
}

static void on_btn_nueva(lv_event_t* e)
{
    AppEvent evt = { 2, 1 };
    xQueueSend(state_queue, &evt, 0);
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

lv_obj_t* create_screen_1_active(bool hasTouch, bool uuidIsNull)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    g_btn_continuar = NULL;
    g_btn_nueva = NULL;

    if (uuidIsNull) {
        lv_obj_t* btn = lv_btn_create(screen);
        lv_obj_set_size(btn, 180, 40);
        lv_obj_center(btn);
        lv_obj_set_style_bg_color(btn, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_STATE_DEFAULT);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, currentLang->nueva_conversacion);
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_STATE_DEFAULT);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, on_btn_nueva, LV_EVENT_CLICKED, NULL);
        g_btn_nueva = btn;
        g_lbl_nueva = lbl;
        g_selected_option = 1;
    } else {
        lv_obj_t* btn1 = lv_btn_create(screen);
        lv_obj_set_size(btn1, 180, 40);
        lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -28);
        lv_obj_t* lbl1 = lv_label_create(btn1);
        lv_label_set_text(lbl1, currentLang->continuar_conversacion);
        lv_obj_center(lbl1);
        lv_obj_add_event_cb(btn1, on_btn_continuar, LV_EVENT_CLICKED, NULL);
        g_btn_continuar = btn1;
        g_lbl_continuar = lbl1;

        lv_obj_t* btn2 = lv_btn_create(screen);
        lv_obj_set_size(btn2, 180, 40);
        lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 28);
        lv_obj_t* lbl2 = lv_label_create(btn2);
        lv_label_set_text(lbl2, currentLang->nueva_conversacion);
        lv_obj_center(lbl2);
        lv_obj_add_event_cb(btn2, on_btn_nueva, LV_EVENT_CLICKED, NULL);
        g_btn_nueva = btn2;
        g_lbl_nueva = lbl2;

        g_selected_option = 0;
        highlight_selection();
    }

    return screen;
}

lv_obj_t* create_screen_2_record()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->grabar_mensaje);

    return screen;
}

lv_obj_t* create_screen_2b_listening()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->escuchando);

    return screen;
}

lv_obj_t* create_screen_3_sending()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->enviando_mensaje);

    return screen;
}

lv_obj_t* create_screen_3b_discarded()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->mensaje_descartado);

    return screen;
}

lv_obj_t* create_screen_4_confirm(const char* transcribedText, bool hasTouch)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    lv_obj_t* cont = lv_obj_create(screen);
    lv_obj_set_size(cont, 180, 108);
    lv_obj_set_pos(cont, 10, STATUS_BAR_H + 4);
    lv_obj_set_style_border_width(cont, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 4, LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* text_label = lv_label_create(cont);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text_label, lv_pct(100));
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_text(text_label, transcribedText);
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    lv_obj_t* btn_send = lv_btn_create(screen);
    lv_obj_set_size(btn_send, 160, 36);
    lv_obj_align(btn_send, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_t* lbl_send = lv_label_create(btn_send);
    lv_label_set_text(lbl_send, currentLang->enviar);
    lv_obj_center(lbl_send);
    lv_obj_add_event_cb(btn_send, on_btn_continuar, LV_EVENT_CLICKED, NULL);
    g_btn_continuar = btn_send;
    g_lbl_continuar = lbl_send;

    lv_obj_t* btn_cancel = lv_btn_create(screen);
    lv_obj_set_size(btn_cancel, 160, 36);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, currentLang->cancelar);
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, on_btn_nueva, LV_EVENT_CLICKED, NULL);
    g_btn_nueva = btn_cancel;
    g_lbl_nueva = lbl_cancel;

    g_selected_option = 0;
    highlight_selection();

    return screen;
}

lv_obj_t* create_screen_5_waiting()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, currentLang->esperando_respuesta);

    return screen;
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
