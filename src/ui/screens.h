#ifndef SCREENS_H
#define SCREENS_H

#include "lvgl.h"

lv_obj_t* create_screen_0_deep_sleep(int sleep_counter);
lv_obj_t* create_screen_1_active(bool hasTouch, bool uuidIsNull);
lv_obj_t* create_screen_2_record();
lv_obj_t* create_screen_2b_listening();
lv_obj_t* create_screen_3_sending();
lv_obj_t* create_screen_3b_discarded();
lv_obj_t* create_screen_4_confirm(const char* transcribedText, bool hasTouch);
lv_obj_t* create_screen_5_waiting();
lv_obj_t* create_screen_6_response(const char* agentText);

#endif
