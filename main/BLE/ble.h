#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============ Standard BLE UUIDs ============

// Battery Service (standard BLE SIG)
#define BLE_UUID_BATTERY_SERVICE            0x180F
#define BLE_UUID_BATTERY_LEVEL_CHAR         0x2A19  // uint8_t 0-100%

// Device Information Service (standard BLE SIG)
#define BLE_UUID_DEVICE_INFO_SERVICE        0x180A
#define BLE_UUID_MANUFACTURER_NAME_CHAR     0x2A29
#define BLE_UUID_MODEL_NUMBER_CHAR          0x2A24
#define BLE_UUID_FIRMWARE_REV_CHAR          0x2A26

// Custom Services (128-bit UUIDs to be defined)
// File Service: handles file listing, operations, transfer
// Audio Control Service: play, pause, next, prev, volume
// Device Status Service: extended status notifications

// File operation types
typedef enum {
    BLE_FILE_OP_LIST = 0,       // List files
    BLE_FILE_OP_DELETE,         // Delete file
    BLE_FILE_OP_RENAME,         // Rename file
    BLE_FILE_OP_GET_INFO,       // Get file info (size, etc.)
} ble_file_op_t;

// File transfer direction
typedef enum {
    BLE_TRANSFER_DOWNLOAD = 0,  // Device -> Phone
    BLE_TRANSFER_UPLOAD,        // Phone -> Device
} ble_transfer_dir_t;

// File transfer state
typedef enum {
    BLE_TRANSFER_IDLE = 0,
    BLE_TRANSFER_IN_PROGRESS,
    BLE_TRANSFER_COMPLETE,
    BLE_TRANSFER_ERROR,
} ble_transfer_state_t;

// File info structure
typedef struct {
    char name[128];
    uint32_t size;
    bool is_directory;
} ble_file_info_t;

// Transfer progress callback
typedef void (*ble_transfer_progress_cb_t)(uint32_t bytes_transferred, uint32_t total_bytes);

// ============ Core BLE Functions ============

// Initialize BLE subsystem
esp_err_t ble_init(void);

// Start BLE advertising
esp_err_t ble_start_advertising(void);

// Stop BLE advertising
esp_err_t ble_stop_advertising(void);

// Check connection status
bool ble_is_connected(void);

// Check advertising status
bool ble_is_advertising(void);

// Button handler for long press (starts BLE)
void ble_button_handler(void);

// ============ File Listing ============

// Get list of all files recursively
// Callback is called for each file found
typedef void (*ble_file_list_cb_t)(const ble_file_info_t *file_info, void *user_data);
esp_err_t ble_list_files(const char *path, ble_file_list_cb_t callback, void *user_data);

// Get file count in directory (recursive)
int ble_get_file_count(const char *path);

// ============ File Operations ============

// Delete a file
esp_err_t ble_delete_file(const char *path);

// Rename a file
esp_err_t ble_rename_file(const char *old_path, const char *new_path);

// Get file info
esp_err_t ble_get_file_info(const char *path, ble_file_info_t *info);

// ============ File Transfer ============

// Start file download (device -> phone)
// Returns transfer handle or negative error
esp_err_t ble_start_download(const char *file_path, ble_transfer_progress_cb_t progress_cb);

// Start file upload (phone -> device)
// file_path: destination path on device
esp_err_t ble_start_upload(const char *file_path, uint32_t file_size, ble_transfer_progress_cb_t progress_cb);

// Cancel ongoing transfer
esp_err_t ble_cancel_transfer(void);

// Get transfer state
ble_transfer_state_t ble_get_transfer_state(void);

// Get transfer progress (bytes transferred)
uint32_t ble_get_transfer_progress(void);

// ============ Playlist Control (via BLE) ============

// These allow the app to control playback
esp_err_t ble_cmd_play(void);
esp_err_t ble_cmd_pause(void);
esp_err_t ble_cmd_next(void);
esp_err_t ble_cmd_prev(void);
esp_err_t ble_cmd_set_volume(uint8_t level);  // 0-4

// ============ Status Notifications ============

// Get device status for BLE notification
typedef struct {
    uint8_t audio_state;        // audio_state_t
    uint8_t volume_level;       // 0-4
    uint8_t battery_percent;    // 0-100% (standard BLE battery level)
    uint16_t battery_mv;        // Battery voltage in mV
    bool is_charging;
    uint8_t playlist_index;
    uint8_t playlist_count;
    char current_track[64];
} ble_device_status_t;

esp_err_t ble_get_device_status(ble_device_status_t *status);

// Get battery level for standard BLE Battery Service (0-100%)
uint8_t ble_get_battery_level(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_H
