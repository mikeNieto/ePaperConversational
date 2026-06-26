#ifndef USER_APP_H
#define USER_APP_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "src/display/epaper_driver_bsp.h"
#include "src/power/board_power_bsp.h"
#include "src/ui/screens.h"

extern epaper_driver_display *driver;
extern board_power_bsp_t board_div;
extern bool hasTouch;
extern EventGroupHandle_t touch_event_group;
#define TOUCH_BIT_TAP  (0x01 << 0)

extern TaskHandle_t sleep_timer_handle;

typedef struct {
    uint8_t type;
    uint8_t data;
} AppEvent;

#define EVT_TOGGLE_SELECTION  1
#define EVT_ACTIVATE_OPTION   2
#define EVT_TOUCH_OPTION      3
#define EVT_RECORDING_DONE    4

typedef enum {
    STATE_DEEP_SLEEP = 0,
    STATE_ACTIVE = 1,
    STATE_RECORD = 2,
    STATE_LISTENING = 3,
    STATE_SENDING = 4,
    STATE_CONFIRM = 5,
    STATE_WAITING = 6,
    STATE_RESPONSE = 7,
    STATE_DISCARDED = 8
} AppState;

extern AppState g_app_state;
extern lv_obj_t* g_btn_continuar;
extern lv_obj_t* g_btn_nueva;
extern lv_obj_t* g_lbl_continuar;
extern lv_obj_t* g_lbl_nueva;
extern int g_selected_option;

void generate_uuid(char* buf, size_t len);
void switch_state(AppState new_state);
void highlight_selection(void);

#ifdef __cplusplus
extern "C" {
#endif

void user_app_init(void);
void lvgl_port_init(void);
void user_ui_init(void);
bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);
void user_button_init(void);
void button_task(void *arg);
bool detectTouch(void);
void touch_task(void *arg);
void battery_task(void *arg);
void display_light_init(void);
void enter_deep_sleep(void);
void enter_deep_sleep_light(void);
void activity_feed(void);
void deep_sleep_timer_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif
