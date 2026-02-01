#ifndef VOLUME_H
#define VOLUME_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOLUME_MUTE = 0,    // -64 dB (minimum)
    VOLUME_LOW = 1,     // -32 dB
    VOLUME_MEDIUM = 2,  // 0 dB (unity gain)
    VOLUME_HIGH = 3,    // 32 dB
    VOLUME_MAX = 4      // 63 dB (maximum)
} volume_level_t;

// Initialize volume control
void volume_init(void);

// Cycle to next volume level (wraps around: MAX -> MUTE)
volume_level_t volume_cycle(void);

// Set specific volume level
void volume_set_level(volume_level_t level);

// Get current volume level
volume_level_t volume_get_level(void);

// Get raw volume value in dB (-64 to 63 range)
int volume_get_raw_value(void);

// Save volume level to NVS
esp_err_t volume_save_to_nvs(void);

// Load volume level from NVS
esp_err_t volume_load_from_nvs(void);

#ifdef __cplusplus
}
#endif

#endif // VOLUME_H
