#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYLIST_MAX_FILES 100
#define PLAYLIST_MAX_PATH_LEN 128

// Initialize playlist - scans storage for audio files
esp_err_t playlist_init(void);

// Rescan storage for audio files (call after new recording)
esp_err_t playlist_rescan(void);

// Get current track path (returns NULL if empty)
const char* playlist_get_current(void);

// Advance to next track, returns new path (wraps around)
const char* playlist_next(void);

// Go to previous track, returns new path (wraps around)
const char* playlist_prev(void);

// Jump to specific track by index, returns path or NULL if invalid
const char* playlist_select(int index);

// Get playlist info
int playlist_get_count(void);
int playlist_get_current_index(void);

// Check if playlist is empty
bool playlist_is_empty(void);

#ifdef __cplusplus
}
#endif

#endif // PLAYLIST_H
