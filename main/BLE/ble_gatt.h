#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdint.h>
#include <esp_err.h>
#include <host/ble_hs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GATT server and register services
 *
 * Must be called after NimBLE host is initialized.
 *
 * @return 0 on success, error code otherwise
 */
int ble_gatt_svr_init(void);

/**
 * @brief GATT service registration callback
 *
 * Called by NimBLE when services are registered.
 */
void ble_gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

/**
 * @brief Start sending file list notifications
 *
 * Sends file entries one by one via notifications on the file list characteristic.
 *
 * @param conn_handle BLE connection handle
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_send_file_list(uint16_t conn_handle);

/**
 * @brief Get the current BLE connection handle
 *
 * @return Connection handle, or BLE_HS_CONN_HANDLE_NONE if not connected
 */
uint16_t ble_gatt_get_conn_handle(void);

/**
 * @brief Set the current BLE connection handle
 *
 * @param conn_handle New connection handle
 */
void ble_gatt_set_conn_handle(uint16_t conn_handle);

/**
 * @brief Notify authentication status change
 */
void ble_gatt_notify_auth_status(void);

/**
 * @brief Update the standard Battery Service level
 *
 * @param level Battery level 0-100%
 */
void ble_gatt_update_battery_level(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif // BLE_GATT_H
