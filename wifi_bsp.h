#ifndef WIFI_BSP_H
#define WIFI_BSP_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

extern EventGroupHandle_t wifi_event_group;

#define WIFI_BIT_CONNECTED    (0x01 << 0)
#define WIFI_BIT_DISCONNECTED (0x01 << 1)

void wifi_init(void);
bool wifi_is_connected(void);
void wifi_task(void *arg);
void wifi_led_write(bool on);

#ifdef __cplusplus
}
#endif

#endif
