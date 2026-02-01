#include "playlist.h"
#include "../Storage/storage.h"
#include <string.h>
#include <esp_log.h>

static const char *TAG = "Playlist";

// Simple array-based playlist
static char playlist_paths[PLAYLIST_MAX_FILES][PLAYLIST_MAX_PATH_LEN];
static int playlist_count = 0;
static int current_index = 0;

// Callback for storage scanning
static void scan_callback(const char *file_path, void *user_data) {
    if (playlist_count >= PLAYLIST_MAX_FILES) {
        ESP_LOGW(TAG, "Playlist full, ignoring: %s", file_path);
        return;
    }

    strncpy(playlist_paths[playlist_count], file_path, PLAYLIST_MAX_PATH_LEN - 1);
    playlist_paths[playlist_count][PLAYLIST_MAX_PATH_LEN - 1] = '\0';
    ESP_LOGI(TAG, "Added to playlist [%d]: %s", playlist_count, file_path);
    playlist_count++;
}

esp_err_t playlist_init(void) {
    playlist_count = 0;
    current_index = 0;

    esp_err_t ret = storage_scan_audio_files(scan_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to scan audio files");
        return ret;
    }

    ESP_LOGI(TAG, "========== PLAYLIST INITIALIZED ==========");
    ESP_LOGI(TAG, "Total tracks: %d", playlist_count);
    for (int i = 0; i < playlist_count; i++) {
        // Extract just filename from full path for cleaner output
        const char *filename = strrchr(playlist_paths[i], '/');
        if (filename) {
            filename++;  // Skip the '/'
        } else {
            filename = playlist_paths[i];
        }
        ESP_LOGI(TAG, "  [%d] %s", i + 1, filename);
    }
    ESP_LOGI(TAG, "==========================================");

    return ESP_OK;
}

esp_err_t playlist_rescan(void) {
    ESP_LOGI(TAG, "Rescanning playlist...");

    // Remember current track path if possible
    char current_track[PLAYLIST_MAX_PATH_LEN] = {0};
    if (playlist_count > 0 && current_index < playlist_count) {
        strncpy(current_track, playlist_paths[current_index], PLAYLIST_MAX_PATH_LEN - 1);
    }

    // Clear and rescan
    playlist_count = 0;
    current_index = 0;

    esp_err_t ret = storage_scan_audio_files(scan_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to rescan audio files");
        return ret;
    }

    // Try to restore position to previous track
    if (current_track[0] != '\0') {
        for (int i = 0; i < playlist_count; i++) {
            if (strcmp(playlist_paths[i], current_track) == 0) {
                current_index = i;
                ESP_LOGI(TAG, "Restored position to track %d", current_index);
                break;
            }
        }
    }

    ESP_LOGI(TAG, "========== PLAYLIST RESCANNED ==========");
    ESP_LOGI(TAG, "Total tracks: %d", playlist_count);
    for (int i = 0; i < playlist_count; i++) {
        const char *filename = strrchr(playlist_paths[i], '/');
        if (filename) {
            filename++;
        } else {
            filename = playlist_paths[i];
        }
        ESP_LOGI(TAG, "  [%d] %s%s", i + 1, filename,
                 (i == current_index) ? " <-- current" : "");
    }
    ESP_LOGI(TAG, "=========================================");

    return ESP_OK;
}

const char* playlist_get_current(void) {
    if (playlist_count == 0) {
        return NULL;
    }
    return playlist_paths[current_index];
}

const char* playlist_next(void) {
    if (playlist_count == 0) {
        return NULL;
    }

    current_index = (current_index + 1) % playlist_count;
    ESP_LOGI(TAG, "Next track [%d/%d]: %s",
             current_index + 1, playlist_count, playlist_paths[current_index]);
    return playlist_paths[current_index];
}

const char* playlist_prev(void) {
    if (playlist_count == 0) {
        return NULL;
    }

    current_index = (current_index - 1 + playlist_count) % playlist_count;
    ESP_LOGI(TAG, "Previous track [%d/%d]: %s",
             current_index + 1, playlist_count, playlist_paths[current_index]);
    return playlist_paths[current_index];
}

const char* playlist_select(int index) {
    if (playlist_count == 0 || index < 0 || index >= playlist_count) {
        ESP_LOGW(TAG, "Invalid playlist index: %d (count: %d)", index, playlist_count);
        return NULL;
    }

    current_index = index;
    ESP_LOGI(TAG, "Selected track [%d/%d]: %s",
             current_index + 1, playlist_count, playlist_paths[current_index]);
    return playlist_paths[current_index];
}

int playlist_get_count(void) {
    return playlist_count;
}

int playlist_get_current_index(void) {
    return current_index;
}

bool playlist_is_empty(void) {
    return (playlist_count == 0);
}
