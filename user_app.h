#ifndef USER_APP_H
#define USER_APP_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "src/display/epaper_driver_bsp.h"
#include "src/power/board_power_bsp.h"

extern epaper_driver_display *driver;
extern board_power_bsp_t board_div;
extern bool hasTouch;
extern EventGroupHandle_t touch_event_group;
#define TOUCH_BIT_TAP  (0x01 << 0)

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

#ifdef __cplusplus
}
#endif

#endif
