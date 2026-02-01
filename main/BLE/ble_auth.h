#ifndef BLE_AUTH_H
#define BLE_AUTH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// Authentication key size (fixed 32 bytes)
#define BLE_AUTH_KEY_SIZE 32

/**
 * @brief Load authentication key from NVS
 *
 * Should be called during BLE initialization.
 * If no key is stored, device enters first-pairing mode.
 *
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no key stored
 */
esp_err_t ble_auth_load_key(void);

/**
 * @brief Save authentication key to NVS
 *
 * @param key Pointer to 32-byte key
 * @param len Length of key (must be BLE_AUTH_KEY_SIZE)
 * @return ESP_OK on success
 */
esp_err_t ble_auth_save_key(const uint8_t *key, size_t len);

/**
 * @brief Clear stored authentication key from NVS
 *
 * This performs a factory reset of the authentication.
 * Device will enter first-pairing mode on next connection.
 * Requires the session to be authenticated first.
 *
 * @return ESP_OK on success
 */
esp_err_t ble_auth_clear_key(void);

/**
 * @brief Check if a stored key exists
 *
 * @return true if a key is stored in NVS
 */
bool ble_auth_has_stored_key(void);

/**
 * @brief Check provided key against stored key
 *
 * If no key is stored (first pairing), the provided key is saved
 * and session is marked as authenticated.
 *
 * @param key Pointer to key to check
 * @param len Length of key
 * @return true if key matches or first pairing succeeded
 */
bool ble_auth_check_key(const uint8_t *key, size_t len);

/**
 * @brief Check if current session is authenticated
 *
 * Returns true if:
 * - No key is stored (first-pairing mode)
 * - Key was provided and matched stored key
 *
 * @return true if session is authenticated
 */
bool ble_auth_is_authenticated(void);

/**
 * @brief Called when BLE connection is disconnected
 *
 * Clears session authentication state.
 * Next connection will need to re-authenticate.
 */
void ble_auth_on_disconnect(void);

/**
 * @brief Get authentication status byte for BLE response
 *
 * @return 0x01 if authenticated, 0x00 if not
 */
uint8_t ble_auth_get_status_byte(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_AUTH_H
