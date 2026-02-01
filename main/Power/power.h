#ifndef POWER_H
#define POWER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Li-ion battery voltage thresholds (mV)
#define BATTERY_VOLTAGE_FULL    4200    // 100%
#define BATTERY_VOLTAGE_NOMINAL 3700    // ~50%
#define BATTERY_VOLTAGE_EMPTY   3000    // 0%

void init_power_measurement(void);
bool is_battery_charging(void);
bool is_power_detected(void);
uint16_t get_bat_voltage(void);

// Get battery level as percentage (0-100)
uint8_t get_battery_percent(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_H