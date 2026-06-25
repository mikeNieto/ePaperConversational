#ifndef BUTTON_BSP_H
#define BUTTON_BSP_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

extern EventGroupHandle_t boot_groups;
extern EventGroupHandle_t pwr_groups;

#define BTN_BIT(x)  ((uint32_t)0x01 << (x))
#define BTN_GET(x,y) (((uint32_t)(x) >> (y)) & 0x01)
#define BTN_ALL_BITS 0x0000000f

/* BOOT button event bits (spec) */
#define BOOT_BIT_SINGLE  0
#define BOOT_BIT_LONG    1
#define BOOT_BIT_UP      2
#define BOOT_BIT_DOUBLE  3

/* PWR button event bits (spec - different order) */
#define PWR_BIT_SINGLE   0
#define PWR_BIT_LONG     1
#define PWR_BIT_DOUBLE   2
#define PWR_BIT_UP       3

void user_button_init(void);

#ifdef __cplusplus
}
#endif

#endif
