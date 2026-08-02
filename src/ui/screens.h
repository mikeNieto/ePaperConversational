#ifndef SCREENS_H
#define SCREENS_H

#include "lvgl.h"
#include <stdint.h>

lv_obj_t* create_screen_0_deep_sleep(int sleep_counter);
lv_obj_t* create_screen_connecting(void);
lv_obj_t* create_screen_2_record(void);
lv_obj_t* create_screen_2b_listening(void);
lv_obj_t* create_screen_receiving(void);
void update_screen_receiving_status(const char* status);
void queue_screen_receiving_status(const char* status);
int32_t get_receiving_scroll_y(void);
lv_obj_t* create_screen_6_response(const char* agentText, int32_t scroll_y);
lv_obj_t* create_screen_settings(const char* ssid, const char* lang_name);

#endif
