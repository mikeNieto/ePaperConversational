#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "lvgl.h"

lv_obj_t* create_status_bar(lv_obj_t* parent);
void update_status_bar(lv_obj_t* bar, bool wifiOk, int batteryPct);

#endif
