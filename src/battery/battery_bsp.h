#ifndef BATTERY_BSP_H
#define BATTERY_BSP_H

#ifdef __cplusplus
extern "C" {
#endif

void battery_init(void);
float battery_get_voltage(void);
int battery_get_percentage(void);
int battery_voltage_to_percentage(float voltage);
void battery_get_status(float* voltage, int* percentage);

#ifdef __cplusplus
}
#endif

#endif
