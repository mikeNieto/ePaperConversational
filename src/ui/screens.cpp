#include "screens.h"
#include "status_bar.h"

#define STATUS_BAR_H 24
#define SCR_W 200
#define SCR_H 200
#define CONTENT_Y (STATUS_BAR_H + 4)
#define CONTENT_H (SCR_H - STATUS_BAR_H)

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
    lv_label_set_text_fmt(label, "Durmiendo... %d", sleep_counter);
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

    if (uuidIsNull) {
        lv_obj_t* btn = lv_btn_create(screen);
        lv_obj_set_size(btn, 180, 40);
        lv_obj_center(btn);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "Nueva Conversacion");
        lv_obj_center(lbl);
    } else {
        lv_obj_t* btn1 = lv_btn_create(screen);
        lv_obj_set_size(btn1, 180, 40);
        lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -28);
        lv_obj_t* lbl1 = lv_label_create(btn1);
        lv_label_set_text(lbl1, "Continuar Conversacion");
        lv_obj_center(lbl1);

        lv_obj_t* btn2 = lv_btn_create(screen);
        lv_obj_set_size(btn2, 180, 40);
        lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 28);
        lv_obj_t* lbl2 = lv_label_create(btn2);
        lv_label_set_text(lbl2, "Nueva Conversacion");
        lv_obj_center(lbl2);
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
    center_label(screen, "Grabar Mensaje");

    return screen;
}

lv_obj_t* create_screen_2b_listening()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, "Escuchando...");

    return screen;
}

lv_obj_t* create_screen_3_sending()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, "Enviando Mensaje...");

    return screen;
}

lv_obj_t* create_screen_3b_discarded()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, "Mensaje descartado!!");

    return screen;
}

lv_obj_t* create_screen_4_confirm(const char* transcribedText, bool hasTouch)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    lv_obj_t* text_label = lv_label_create(screen);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text_label, 180);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_text(text_label, transcribedText);
    lv_obj_set_pos(text_label, 10, STATUS_BAR_H + 10);
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    lv_obj_t* btn_send = lv_btn_create(screen);
    lv_obj_set_size(btn_send, 160, 36);
    lv_obj_align(btn_send, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_t* lbl_send = lv_label_create(btn_send);
    lv_label_set_text(lbl_send, "Enviar");
    lv_obj_center(lbl_send);

    lv_obj_t* btn_cancel = lv_btn_create(screen);
    lv_obj_set_size(btn_cancel, 160, 36);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancelar");
    lv_obj_center(lbl_cancel);

    return screen;
}

lv_obj_t* create_screen_5_waiting()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);
    center_label(screen, "Esperando Respuesta...");

    return screen;
}

lv_obj_t* create_screen_6_response(const char* agentText)
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen);

    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 180);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_text(label, agentText);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    return screen;
}
