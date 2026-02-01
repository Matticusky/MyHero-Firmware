#include "volume.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>

static const char *TAG = "Volume";

// NVS namespace and key
#define NVS_NAMESPACE "volume"
#define NVS_KEY_LEVEL "level"

// Volume level to raw value mapping in dB (-64 to 63 range)
static const int volume_raw_values[5] = {
    -64,  // VOLUME_MUTE - minimum
    -32,  // VOLUME_LOW - -32 dB
    0,    // VOLUME_MEDIUM - 0 dB (unity gain)
    32,   // VOLUME_HIGH - +32 dB
    63    // VOLUME_MAX - maximum
};

static volume_level_t current_level = VOLUME_MEDIUM;  // Default to 50%

void volume_init(void) {
    current_level = VOLUME_MEDIUM;
    ESP_LOGI(TAG, "Volume initialized to level %d (%d dB)",
             current_level, volume_raw_values[current_level]);
}

volume_level_t volume_cycle(void) {
    current_level = (current_level + 1) % 5;  // Cycle through 0-4

    ESP_LOGI(TAG, "Volume cycled to level %d (%d dB)",
             current_level, volume_raw_values[current_level]);

    // Auto-save to NVS on change
    volume_save_to_nvs();

    return current_level;
}

void volume_set_level(volume_level_t level) {
    if (level > VOLUME_MAX) {
        ESP_LOGW(TAG, "Invalid volume level %d, clamping to MAX", level);
        level = VOLUME_MAX;
    }

    current_level = level;
    ESP_LOGI(TAG, "Volume set to level %d (%d dB)",
             current_level, volume_raw_values[current_level]);
}

volume_level_t volume_get_level(void) {
    return current_level;
}

int volume_get_raw_value(void) {
    return volume_raw_values[current_level];
}

esp_err_t volume_save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_LEVEL, (uint8_t)current_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write volume to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Volume level %d saved to NVS", current_level);
    }

    nvs_close(handle);
    return err;
}

esp_err_t volume_load_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved volume in NVS, using default");
        } else {
            ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        }
        return err;
    }

    uint8_t saved_level;
    err = nvs_get_u8(handle, NVS_KEY_LEVEL, &saved_level);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved volume in NVS, using default");
        } else {
            ESP_LOGE(TAG, "Failed to read volume from NVS: %s", esp_err_to_name(err));
        }
        nvs_close(handle);
        return err;
    }

    if (saved_level > VOLUME_MAX) {
        ESP_LOGW(TAG, "Invalid saved volume %d, using default", saved_level);
        saved_level = VOLUME_MEDIUM;
    }

    current_level = (volume_level_t)saved_level;
    ESP_LOGI(TAG, "Volume level %d loaded from NVS (%d dB)",
             current_level, volume_raw_values[current_level]);

    nvs_close(handle);
    return ESP_OK;
}
