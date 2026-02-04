#ifndef BLE_TRANSFER_H
#define BLE_TRANSFER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// Chunk size for raw binary transfer (no Base64)
// With MTU=512, max ATT payload is 509 bytes (MTU - 3)
// Use 490 bytes to leave margin for protocol overhead
#define BLE_TRANSFER_CHUNK_SIZE   490

// Internal transfer states (more detailed than public API)
typedef enum {
    BLE_XFER_STATE_IDLE = 0,
    BLE_XFER_STATE_UPLOAD_PENDING,   // Waiting for first data chunk
    BLE_XFER_STATE_UPLOADING,        // Receiving chunks
    BLE_XFER_STATE_DOWNLOAD_PENDING, // About to send file info
    BLE_XFER_STATE_DOWNLOADING,      // Sending chunks
    BLE_XFER_STATE_COMPLETE,
    BLE_XFER_STATE_ERROR,
} ble_xfer_state_t;

// Transfer direction
typedef enum {
    BLE_XFER_DIR_NONE = 0,
    BLE_XFER_DIR_UPLOAD,   // Phone -> Device
    BLE_XFER_DIR_DOWNLOAD, // Device -> Phone
} ble_xfer_dir_t;

/**
 * @brief Initialize transfer module
 */
void ble_transfer_init(void);

/**
 * @brief Start file upload (Phone -> Device)
 *
 * @param filename Filename to create (without /Storage/ prefix)
 * @param total_size Expected file size in bytes
 * @param conn_handle BLE connection handle for notifications
 * @return ESP_OK on success
 */
esp_err_t ble_transfer_start_upload(const char *filename, uint32_t total_size,
                                     uint16_t conn_handle);

/**
 * @brief Start file download (Device -> Phone)
 *
 * @param filename Filename to send (without /Storage/ prefix)
 * @param conn_handle BLE connection handle for notifications
 * @return ESP_OK on success
 */
esp_err_t ble_transfer_start_download(const char *filename, uint16_t conn_handle);

/**
 * @brief Receive a raw binary data chunk (for upload)
 *
 * @param data Raw binary data
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t ble_transfer_receive_chunk(const uint8_t *data, size_t len);

/**
 * @brief Prepare next download chunk into internal buffer
 *
 * Call this to load the next chunk from file into buffer.
 * App will then read via ble_transfer_get_chunk_data().
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FINISHED when complete
 */
esp_err_t ble_transfer_prepare_next_chunk(void);

/**
 * @brief Get pointer to current chunk data for reading
 *
 * @param[out] data Pointer to chunk buffer
 * @param[out] len Length of data in buffer
 * @return ESP_OK if chunk is ready, ESP_ERR_INVALID_STATE if no chunk available
 */
esp_err_t ble_transfer_get_chunk_data(const uint8_t **data, size_t *len);

/**
 * @brief Mark current chunk as read by app, prepare next
 *
 * Called after app reads the chunk. Prepares next chunk and notifies.
 */
void ble_transfer_chunk_read_complete(void);

/**
 * @brief Cancel ongoing transfer
 *
 * Closes file, deletes partial uploads, resets state
 */
void ble_transfer_cancel(void);

/**
 * @brief Get current transfer state
 *
 * @return Current transfer state
 */
ble_xfer_state_t ble_transfer_get_state(void);

/**
 * @brief Get current transfer direction
 *
 * @return Current transfer direction
 */
ble_xfer_dir_t ble_transfer_get_direction(void);

/**
 * @brief Get bytes transferred so far
 *
 * @return Number of bytes transferred
 */
uint32_t ble_transfer_get_progress(void);

/**
 * @brief Get total transfer size
 *
 * @return Total file size in bytes
 */
uint32_t ble_transfer_get_total(void);

/**
 * @brief Get transfer progress as percentage (0-100)
 *
 * @return Progress percentage
 */
uint8_t ble_transfer_get_percent(void);

/**
 * @brief Check if transfer is active
 *
 * @return true if upload or download is in progress
 */
bool ble_transfer_is_active(void);

/**
 * @brief Set the attribute handles for notifications
 *
 * @param ctrl_handle Transfer control characteristic handle
 * @param data_handle Transfer data characteristic handle
 * @param progress_handle Transfer progress characteristic handle
 */
void ble_transfer_set_handles(uint16_t ctrl_handle, uint16_t data_handle,
                               uint16_t progress_handle);

/**
 * @brief Get the file size for download response
 *
 * @return File size set during start_download
 */
uint32_t ble_transfer_get_file_size(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_TRANSFER_H
