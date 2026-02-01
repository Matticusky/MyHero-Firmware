#ifndef INDICATOR_H
#define INDICATOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// LED indicator modes
typedef enum {
    LED_MODE_OFF = 0,           // LED off
    LED_MODE_ON,                // LED continuously on
    LED_MODE_RECORDING,         // 0.1s on, 0.9s off (blink during recording)
    LED_MODE_PLAYING,           // 0.5s on every 5s (pulse while playing)
    LED_MODE_BLE_PAIRING,       // Continuously on (BLE advertising/connected)
    LED_MODE_BLE_TRANSFER,      // Fast pulse: 0.1s on, 0.1s off (file transfer)
    LED_MODE_IDLE,              // Default idle state (off or dim)
} led_mode_t;

// Initialize LED indicator
void init_led_indicator(void);

// Set LED mode (starts the pattern automatically)
void led_set_mode(led_mode_t mode);

// Get current LED mode
led_mode_t led_get_mode(void);

// Direct LED control (for legacy compatibility)
void led_on(void);
void led_off(void);

// Legacy functions (still available but prefer led_set_mode)
void set_led_indicator_frequency(uint32_t frequency);
void set_led_indicator_duty(uint8_t duty);
void start_led_indicator(void);
void stop_led_indicator(void);

#ifdef __cplusplus
}
#endif

#endif // INDICATOR_H
