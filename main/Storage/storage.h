#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// Existing functions
void mount_storage(void);
void get_base_path(char *path, size_t size);
void get_storage_info(size_t *total_size, size_t *used_size, size_t *free_size);
void print_storage_info(void);

// Callback for file scanning
typedef void (*storage_scan_cb_t)(const char *file_path, void *user_data);

// Scan storage for audio files (.wav) and call callback for each
esp_err_t storage_scan_audio_files(storage_scan_cb_t callback, void *user_data);

// Generate unique recording filename (sequential: recording_0001.wav, etc.)
esp_err_t storage_generate_recording_path(char *path_buf, size_t buf_size);

// Check if a file exists
bool storage_file_exists(const char *path);

// Delete all files in storage (for debugging)
esp_err_t storage_delete_all_files(void);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_H
