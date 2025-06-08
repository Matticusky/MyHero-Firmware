#ifndef POWER_H
#define POWER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

void init_power_measurement();
bool is_battery_charging();
bool is_power_detected();
uint16_t get_bat_voltage();

#ifdef __cplusplus
}
#endif
#endif // POWER_H