#ifndef BATTERY_BSP_H
#define BATTERY_BSP_H

#ifdef __cplusplus
extern "C" {
#endif

void battery_init(void);
float battery_get_voltage(void);
int battery_get_percentage(void);

#ifdef __cplusplus
}
#endif

#endif
