#ifndef INDICATOR_H
#define INDICATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

void init_led_indicator();
void set_led_indicator_frequency(uint32_t frequency);
void set_led_indicator_duty(uint8_t duty);
void start_led_indicator();
void stop_led_indicator();

#ifdef __cplusplus
}
#endif
#endif // INDICATOR_H
