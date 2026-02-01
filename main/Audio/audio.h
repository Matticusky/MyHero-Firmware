#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio state machine
typedef enum {
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED,
    AUDIO_STATE_RECORDING
} audio_state_t;

// Initialize audio system
void init_audio_system(void);

// State queries
audio_state_t audio_get_state(void);
bool audio_is_playing(void);
bool audio_is_recording(void);
bool audio_is_paused(void);

// Playback control
esp_err_t audio_play_file(const char *file_path);
void audio_stop_playback(void);
void audio_update_volume(void);

// Recording control
esp_err_t audio_start_recording(void);
void audio_stop_recording(void);

// Get last recorded file path
const char* audio_get_last_recording(void);

// New button handlers for firmware.c
void play_pause_single_handler(void);
void play_pause_double_handler(void);
void record_single_handler(void);
void record_double_handler(void);

// Legacy handlers (deprecated - use new handlers above)
void record_button_press_handler(void);
void play_pause_button_press_handler(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_H
