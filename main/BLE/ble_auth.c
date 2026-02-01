#include "ble_auth.h"

#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>

static const char *TAG = "BLE_AUTH";

// NVS namespace and key
#define NVS_NAMESPACE "ble_auth"
#define NVS_KEY_AUTH  "auth_key"

// Stored authentication key
static uint8_t stored_key[BLE_AUTH_KEY_SIZE];
static size_t stored_key_len = 0;
static bool has_stored_key = false;

// Session authentication state (cleared on disconnect)
static bool session_authenticated = false;

esp_err_t ble_auth_load_key(void) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No auth namespace found - device is in first-pairing mode");
        has_stored_key = false;
        stored_key_len = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    size_t len = BLE_AUTH_KEY_SIZE;
    err = nvs_get_blob(handle, NVS_KEY_AUTH, stored_key, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No auth key stored - device is in first-pairing mode");
        has_stored_key = false;
        stored_key_len = 0;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read auth key: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    } else {
        has_stored_key = true;
        stored_key_len = len;
        ESP_LOGI(TAG, "Auth key loaded (%d bytes)", (int)len);
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t ble_auth_save_key(const uint8_t *key, size_t len) {
    if (!key || len != BLE_AUTH_KEY_SIZE) {
        ESP_LOGE(TAG, "Invalid key: must be exactly %d bytes", BLE_AUTH_KEY_SIZE);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_AUTH, key, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write auth key: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
        // Update cached state
        memcpy(stored_key, key, len);
        stored_key_len = len;
        has_stored_key = true;
        ESP_LOGI(TAG, "Auth key saved successfully");
    }

    nvs_close(handle);
    return err;
}

esp_err_t ble_auth_clear_key(void) {
    if (!session_authenticated) {
        ESP_LOGW(TAG, "Cannot clear key - session not authenticated");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for clear: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY_AUTH);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase auth key: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        // Clear cached state
        memset(stored_key, 0, sizeof(stored_key));
        stored_key_len = 0;
        has_stored_key = false;
        session_authenticated = false;
        ESP_LOGI(TAG, "Auth key cleared - device entering first-pairing mode");
    }

    return err;
}

bool ble_auth_has_stored_key(void) {
    return has_stored_key;
}

bool ble_auth_check_key(const uint8_t *key, size_t len) {
    if (!key || len != BLE_AUTH_KEY_SIZE) {
        ESP_LOGW(TAG, "Invalid key length: expected %d, got %d",
                 BLE_AUTH_KEY_SIZE, (int)len);
        return false;
    }

    if (!has_stored_key) {
        // First pairing - save this key and authenticate
        ESP_LOGI(TAG, "First pairing - saving provided key");
        esp_err_t err = ble_auth_save_key(key, len);
        if (err == ESP_OK) {
            session_authenticated = true;
            ESP_LOGI(TAG, "First pairing successful - session authenticated");
            return true;
        }
        ESP_LOGE(TAG, "Failed to save key during first pairing");
        return false;
    }

    // Compare with stored key
    if (len != stored_key_len) {
        ESP_LOGW(TAG, "Key length mismatch");
        return false;
    }

    if (memcmp(key, stored_key, len) == 0) {
        session_authenticated = true;
        ESP_LOGI(TAG, "Authentication successful");
        return true;
    }

    ESP_LOGW(TAG, "Authentication failed - key mismatch");
    return false;
}

bool ble_auth_is_authenticated(void) {
    // If no key is stored, device is in first-pairing mode - considered authenticated
    if (!has_stored_key) {
        return true;
    }
    return session_authenticated;
}

void ble_auth_on_disconnect(void) {
    session_authenticated = false;
    ESP_LOGI(TAG, "Session authentication cleared on disconnect");
}

uint8_t ble_auth_get_status_byte(void) {
    return ble_auth_is_authenticated() ? 0x01 : 0x00;
}
