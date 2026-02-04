#include "ble.h"
#include "ble_uuids.h"
#include "ble_auth.h"
#include "ble_gatt.h"
#include "ble_transfer.h"
#include "../Storage/storage.h"
#include "../Audio/audio.h"
#include "../Volume/volume.h"
#include "../Playlist/playlist.h"
#include "../Power/power.h"
#include "../Indicator/indicator.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

// NimBLE includes
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_att.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

// Preferred MTU size (max 512 for BLE 4.2+)
#define BLE_PREFERRED_MTU 512

static const char *TAG = "BLE";

// Device name
#define BLE_DEVICE_NAME "MyHero"

// State variables
static bool is_initialized = false;
static bool is_advertising = false;
static bool is_connected = false;
static uint16_t current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t own_addr_type;

// Forward declarations
static void ble_on_reset(int reason);
static void ble_on_sync(void);
static void ble_host_task(void *param);
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

// ============ NimBLE Host Task ============

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    // This function will return only when nimble_port_stop() is executed
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ============ NimBLE Callbacks ============

static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE host reset, reason: %d", reason);
}

static void ble_on_sync(void) {
    int rc;

    ESP_LOGI(TAG, "BLE host synchronized");

    // Determine address type
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }

    // Print our address
    uint8_t addr_val[6] = {0};
    ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    ESP_LOGI(TAG, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X",
             addr_val[5], addr_val[4], addr_val[3],
             addr_val[2], addr_val[1], addr_val[0]);

    // Start advertising if requested
    if (is_advertising) {
        start_advertising();
    }
}

// ============ Advertising ============

static void start_advertising(void) {
    int rc;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    // Configure advertising fields
    memset(&fields, 0, sizeof(fields));

    // Flags: general discoverable, BLE only
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // TX power level
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // Device name
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising fields: %d", rc);
        return;
    }

    // Configure advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // Undirected connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // General discoverable
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;  // 30ms
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;  // 60ms

    // Start advertising
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        is_advertising = false;
        return;
    }

    is_advertising = true;
    ESP_LOGI(TAG, "Advertising started");
}

// ============ GAP Event Handler ============

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg) {
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            // Connection successful
            current_conn_handle = event->connect.conn_handle;
            is_connected = true;
            is_advertising = false;  // Stop advertising - single connection only

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                ESP_LOGI(TAG, "Connected to %02X:%02X:%02X:%02X:%02X:%02X",
                         desc.peer_ota_addr.val[5], desc.peer_ota_addr.val[4],
                         desc.peer_ota_addr.val[3], desc.peer_ota_addr.val[2],
                         desc.peer_ota_addr.val[1], desc.peer_ota_addr.val[0]);
            }

            // Request higher MTU for larger data transfers
            rc = ble_att_set_preferred_mtu(BLE_PREFERRED_MTU);
            if (rc != 0) {
                ESP_LOGW(TAG, "Failed to set preferred MTU: %d", rc);
            }
            rc = ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
            if (rc != 0) {
                ESP_LOGW(TAG, "Failed to request MTU exchange: %d", rc);
            }

            // Request faster connection parameters for higher throughput
            // Min interval: 7.5ms (6 * 1.25ms), Max interval: 15ms (12 * 1.25ms)
            // Latency: 0, Supervision timeout: 4s (400 * 10ms)
            struct ble_gap_upd_params conn_params = {
                .itvl_min = 6,    // 7.5ms (fastest allowed)
                .itvl_max = 12,   // 15ms
                .latency = 0,     // No slave latency for fastest response
                .supervision_timeout = 400,  // 4 seconds
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            rc = ble_gap_update_params(event->connect.conn_handle, &conn_params);
            if (rc != 0) {
                ESP_LOGW(TAG, "Failed to request connection param update: %d", rc);
            } else {
                ESP_LOGI(TAG, "Requested fast connection parameters (7.5-15ms interval)");
            }

            // Update GATT module with connection handle
            ble_gatt_set_conn_handle(current_conn_handle);

            // Update battery level in Battery Service
            ble_gatt_update_battery_level(get_battery_percent());

            // LED stays in BLE mode
            led_set_mode(LED_MODE_BLE_PAIRING);
        } else {
            // Connection failed, resume advertising
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);

        current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        is_connected = false;

        // Clear authentication state
        ble_auth_on_disconnect();

        // Cancel any ongoing transfer
        ble_transfer_cancel();

        // Update GATT module
        ble_gatt_set_conn_handle(BLE_HS_CONN_HANDLE_NONE);

        // Resume advertising
        start_advertising();

        // Keep LED in BLE mode since we're advertising again
        led_set_mode(LED_MODE_BLE_PAIRING);
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "Connection updated; status=%d", event->conn_update.status);
        if (event->conn_update.status == 0) {
            rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            if (rc == 0) {
                ESP_LOGI(TAG, "New conn params: interval=%d (%.2fms), latency=%d, timeout=%d",
                         desc.conn_itvl, desc.conn_itvl * 1.25,
                         desc.conn_latency, desc.supervision_timeout);
            }
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete; reason=%d",
                 event->adv_complete.reason);
        // Restart advertising if not connected
        if (!is_connected) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: conn_handle=%d, cid=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: conn_handle=%d, attr_handle=%d, "
                 "cur_notify=%d, cur_indicate=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        break;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGD(TAG, "Notify TX: status=%d", event->notify_tx.status);
        break;

    default:
        ESP_LOGD(TAG, "GAP event: %d", event->type);
        break;
    }

    return 0;
}

// ============ Core BLE Functions ============

esp_err_t ble_init(void) {
    int rc;

    if (is_initialized) {
        ESP_LOGW(TAG, "BLE already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing BLE...");

    // Initialize NimBLE port
    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE port: %d", rc);
        return ESP_FAIL;
    }

    // Configure NimBLE host
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = ble_gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Disable pairing/bonding (using app-level auth)
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    // Initialize GATT services
    rc = ble_gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initialize GATT services: %d", rc);
        return ESP_FAIL;
    }

    // Set device name
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set device name: %d", rc);
    }

    // Load authentication key from NVS
    ble_auth_load_key();

    // Initialize transfer module
    ble_transfer_init();

    // Start NimBLE host task
    nimble_port_freertos_init(ble_host_task);

    is_initialized = true;
    ESP_LOGI(TAG, "BLE initialized successfully");

    return ESP_OK;
}

esp_err_t ble_start_advertising(void) {
    if (!is_initialized) {
        ESP_LOGW(TAG, "BLE not initialized, initializing now...");
        esp_err_t err = ble_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (is_connected) {
        ESP_LOGW(TAG, "Already connected, cannot advertise");
        return ESP_ERR_INVALID_STATE;
    }

    if (is_advertising) {
        ESP_LOGW(TAG, "Already advertising");
        return ESP_OK;
    }

    is_advertising = true;  // Set flag before sync callback

    // If host is already synced, start advertising now
    if (ble_hs_synced()) {
        start_advertising();
    }
    // Otherwise, ble_on_sync will start advertising

    // Set LED to BLE pairing mode
    led_set_mode(LED_MODE_BLE_PAIRING);

    return ESP_OK;
}

esp_err_t ble_stop_advertising(void) {
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_advertising) {
        ESP_LOGW(TAG, "Not advertising");
        return ESP_OK;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to stop advertising: %d", rc);
        return ESP_FAIL;
    }

    is_advertising = false;
    ESP_LOGI(TAG, "Advertising stopped");

    // If connected, disconnect
    if (is_connected && current_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(current_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    // Restore LED to appropriate mode based on audio state
    audio_state_t audio_state = audio_get_state();
    switch (audio_state) {
        case AUDIO_STATE_RECORDING:
            led_set_mode(LED_MODE_RECORDING);
            break;
        case AUDIO_STATE_PLAYING:
            led_set_mode(LED_MODE_PLAYING);
            break;
        default:
            led_set_mode(LED_MODE_IDLE);
            break;
    }

    return ESP_OK;
}

bool ble_is_connected(void) {
    return is_connected;
}

bool ble_is_advertising(void) {
    return is_advertising;
}

void ble_button_handler(void) {
    ESP_LOGI(TAG, "BLE button handler triggered");

    if (!is_initialized) {
        ble_init();
    }

    if (is_advertising || is_connected) {
        ESP_LOGI(TAG, "Stopping BLE...");
        ble_stop_advertising();
    } else {
        ESP_LOGI(TAG, "Starting BLE advertising...");
        ble_start_advertising();
    }
}

// ============ File Listing ============

static esp_err_t list_files_recursive(const char *path, ble_file_list_cb_t callback, void *user_data) {
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    char full_path[300];
    struct stat st;
    ble_file_info_t file_info;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (stat(full_path, &st) == 0) {
            memset(&file_info, 0, sizeof(file_info));
            strncpy(file_info.name, full_path, sizeof(file_info.name) - 1);
            file_info.size = st.st_size;
            file_info.is_directory = S_ISDIR(st.st_mode);

            // Call callback for this file/directory
            if (callback) {
                callback(&file_info, user_data);
            }

            // Recurse into directories
            if (file_info.is_directory) {
                list_files_recursive(full_path, callback, user_data);
            }
        }
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t ble_list_files(const char *path, ble_file_list_cb_t callback, void *user_data) {
    if (!path) {
        // Default to storage root
        char base_path[32];
        get_base_path(base_path, sizeof(base_path));
        return list_files_recursive(base_path, callback, user_data);
    }
    return list_files_recursive(path, callback, user_data);
}

// Helper for counting files
static int file_count = 0;
static void count_callback(const ble_file_info_t *info, void *user_data) {
    if (!info->is_directory) {
        file_count++;
    }
}

int ble_get_file_count(const char *path) {
    file_count = 0;
    ble_list_files(path, count_callback, NULL);
    return file_count;
}

// ============ File Operations ============

esp_err_t ble_delete_file(const char *path) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Deleting file: %s", path);

    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    if (S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "Directory deletion not supported");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (unlink(path) != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File deleted successfully: %s", path);
    playlist_rescan();

    return ESP_OK;
}

esp_err_t ble_rename_file(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Renaming file: %s -> %s", old_path, new_path);

    if (rename(old_path, new_path) != 0) {
        ESP_LOGE(TAG, "Failed to rename file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File renamed successfully");
    playlist_rescan();

    return ESP_OK;
}

esp_err_t ble_get_file_info(const char *path, ble_file_info_t *info) {
    if (!path || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    memset(info, 0, sizeof(ble_file_info_t));
    strncpy(info->name, path, sizeof(info->name) - 1);
    info->size = st.st_size;
    info->is_directory = S_ISDIR(st.st_mode);

    return ESP_OK;
}

// ============ File Transfer Wrappers ============

esp_err_t ble_start_download(const char *file_path, ble_transfer_progress_cb_t progress_cb) {
    // Extract just filename from path if full path provided
    const char *filename = file_path;
    if (strncmp(file_path, "/Storage/", 9) == 0) {
        filename = file_path + 9;
    }
    return ble_transfer_start_download(filename, current_conn_handle);
}

esp_err_t ble_start_upload(const char *file_path, uint32_t file_size, ble_transfer_progress_cb_t progress_cb) {
    // Extract just filename from path if full path provided
    const char *filename = file_path;
    if (strncmp(file_path, "/Storage/", 9) == 0) {
        filename = file_path + 9;
    }
    return ble_transfer_start_upload(filename, file_size, current_conn_handle);
}

esp_err_t ble_cancel_transfer(void) {
    ble_transfer_cancel();
    return ESP_OK;
}

ble_transfer_state_t ble_get_transfer_state(void) {
    ble_xfer_state_t state = ble_transfer_get_state();
    // Map internal states to public enum
    switch (state) {
        case BLE_XFER_STATE_IDLE:
            return BLE_TRANSFER_IDLE;
        case BLE_XFER_STATE_UPLOAD_PENDING:
        case BLE_XFER_STATE_UPLOADING:
        case BLE_XFER_STATE_DOWNLOAD_PENDING:
        case BLE_XFER_STATE_DOWNLOADING:
            return BLE_TRANSFER_IN_PROGRESS;
        case BLE_XFER_STATE_COMPLETE:
            return BLE_TRANSFER_COMPLETE;
        case BLE_XFER_STATE_ERROR:
            return BLE_TRANSFER_ERROR;
        default:
            return BLE_TRANSFER_IDLE;
    }
}

uint32_t ble_get_transfer_progress(void) {
    return ble_transfer_get_progress();
}

// ============ Playlist Control ============

esp_err_t ble_cmd_play(void) {
    ESP_LOGI(TAG, "BLE command: play");
    audio_state_t state = audio_get_state();

    if (state == AUDIO_STATE_IDLE) {
        const char *track = playlist_get_current();
        if (track) {
            return audio_play_file(track);
        }
        return ESP_ERR_NOT_FOUND;
    } else if (state == AUDIO_STATE_PAUSED) {
        play_pause_single_handler();  // Resume
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t ble_cmd_pause(void) {
    ESP_LOGI(TAG, "BLE command: pause");
    if (audio_get_state() == AUDIO_STATE_PLAYING) {
        play_pause_single_handler();
    }
    return ESP_OK;
}

esp_err_t ble_cmd_next(void) {
    ESP_LOGI(TAG, "BLE command: next");
    play_pause_double_handler();
    return ESP_OK;
}

esp_err_t ble_cmd_prev(void) {
    ESP_LOGI(TAG, "BLE command: prev");
    const char *track = playlist_prev();
    if (track) {
        audio_state_t state = audio_get_state();
        if (state == AUDIO_STATE_PLAYING || state == AUDIO_STATE_PAUSED) {
            audio_stop_playback();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        return audio_play_file(track);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ble_cmd_set_volume(uint8_t level) {
    ESP_LOGI(TAG, "BLE command: set volume to %d", level);
    if (level > 4) {
        level = 4;
    }
    volume_set_level((volume_level_t)level);
    return ESP_OK;
}

// ============ Device Status ============

esp_err_t ble_get_device_status(ble_device_status_t *status) {
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(ble_device_status_t));

    status->audio_state = (uint8_t)audio_get_state();
    status->volume_level = (uint8_t)volume_get_level();
    status->battery_percent = get_battery_percent();
    status->battery_mv = get_bat_voltage();
    status->is_charging = is_battery_charging();
    status->playlist_index = (uint8_t)playlist_get_current_index();
    status->playlist_count = (uint8_t)playlist_get_count();

    const char *current = playlist_get_current();
    if (current) {
        const char *filename = strrchr(current, '/');
        if (filename) {
            filename++;
        } else {
            filename = current;
        }
        strncpy(status->current_track, filename, sizeof(status->current_track) - 1);
    }

    return ESP_OK;
}

uint8_t ble_get_battery_level(void) {
    return get_battery_percent();
}
