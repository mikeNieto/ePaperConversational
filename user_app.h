#ifndef USER_APP_H
#define USER_APP_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "src/display/epaper_driver_bsp.h"
#include "src/power/board_power_bsp.h"
#include "src/ui/screens.h"
#include "user_config.h"

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

#define EVT_WS_CONNECTED      1
#define EVT_WS_DISCONNECTED   2
#define EVT_WS_ERROR          3
#define EVT_START_RECORDING   4
#define EVT_STOP_RECORDING    5
#define EVT_RECORDING_DONE    6
#define EVT_RESPONSE_READY    7
#define EVT_NEXT_MESSAGE      8
#define EVT_WS_RECONNECT      9
#define EVT_DISCARD           10

typedef enum {
    STATE_CONNECTING = 0,
    STATE_RECORD = 1,
    STATE_LISTENING = 2,
    STATE_RECEIVING = 3,
    STATE_RESPONSE = 4
} AppState;

extern AppState g_app_state;
extern char g_agent_text[AGENT_TEXT_SIZE];

void switch_state(AppState new_state);

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
