#include "ble_gatt.h"
#include "ble_uuids.h"
#include "ble_auth.h"
#include "ble_transfer.h"
#include "../Storage/storage.h"
#include "../Playlist/playlist.h"
#include "../Power/power.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>

#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <services/bas/ble_svc_bas.h>

static const char *TAG = "BLE_GATT";

// Current connection handle
static uint16_t current_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// Characteristic value handles (set during registration)
static uint16_t auth_status_handle;
static uint16_t auth_key_write_handle;
static uint16_t auth_key_clear_handle;
static uint16_t file_list_handle;
static uint16_t file_delete_handle;
static uint16_t transfer_ctrl_handle;
static uint16_t transfer_data_handle;
static uint16_t transfer_progress_handle;

// UUID declarations (static instances)
static const ble_uuid128_t auth_svc_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x01, 0x00, 0x00, 0x00);

static const ble_uuid128_t auth_key_write_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x01, 0x01, 0x00, 0x00);

static const ble_uuid128_t auth_status_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x02, 0x01, 0x00, 0x00);

static const ble_uuid128_t auth_key_clear_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x03, 0x01, 0x00, 0x00);

static const ble_uuid128_t file_svc_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x02, 0x00, 0x00, 0x00);

static const ble_uuid128_t file_list_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x01, 0x02, 0x00, 0x00);

static const ble_uuid128_t file_delete_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x02, 0x02, 0x00, 0x00);

static const ble_uuid128_t transfer_ctrl_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x03, 0x02, 0x00, 0x00);

static const ble_uuid128_t transfer_data_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x04, 0x02, 0x00, 0x00);

static const ble_uuid128_t transfer_progress_uuid = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x42, 0x48, 0x59, 0x4D, 0x05, 0x02, 0x00, 0x00);

// Forward declarations for access callbacks
static int auth_key_write_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int auth_status_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int auth_key_clear_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int file_list_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int file_delete_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int transfer_ctrl_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int transfer_data_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int transfer_progress_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg);

// ============ Service Definitions ============

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    // Auth Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &auth_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Auth Key Write - write 32-byte key
                .uuid = &auth_key_write_uuid.u,
                .access_cb = auth_key_write_access,
                .val_handle = &auth_key_write_handle,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                // Auth Status - read/notify auth state
                .uuid = &auth_status_uuid.u,
                .access_cb = auth_status_access,
                .val_handle = &auth_status_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Auth Key Clear - write to clear stored key (requires auth)
                .uuid = &auth_key_clear_uuid.u,
                .access_cb = auth_key_clear_access,
                .val_handle = &auth_key_clear_handle,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 } // Terminator
        },
    },
    // File Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &file_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // File List - read triggers file listing via notify
                .uuid = &file_list_uuid.u,
                .access_cb = file_list_access,
                .val_handle = &file_list_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // File Delete - write path to delete
                .uuid = &file_delete_uuid.u,
                .access_cb = file_delete_access,
                .val_handle = &file_delete_handle,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                // Transfer Control - upload/download requests
                .uuid = &transfer_ctrl_uuid.u,
                .access_cb = transfer_ctrl_access,
                .val_handle = &transfer_ctrl_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Transfer Data - file data chunks
                // Write: upload chunks, Read: download chunks
                .uuid = &transfer_data_uuid.u,
                .access_cb = transfer_data_access,
                .val_handle = &transfer_data_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Transfer Progress - current progress
                .uuid = &transfer_progress_uuid.u,
                .access_cb = transfer_progress_access,
                .val_handle = &transfer_progress_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 } // Terminator
        },
    },
    { 0 } // Terminator
};

// ============ Access Callbacks ============

static int auth_key_write_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != BLE_AUTH_KEY_SIZE) {
        ESP_LOGW(TAG, "Invalid auth key length: %d (expected %d)", len, BLE_AUTH_KEY_SIZE);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t key[BLE_AUTH_KEY_SIZE];
    int rc = ble_hs_mbuf_to_flat(ctxt->om, key, len, NULL);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    bool success = ble_auth_check_key(key, len);
    ESP_LOGI(TAG, "Auth key write: %s", success ? "SUCCESS" : "FAILED");

    // Notify auth status change
    ble_gatt_notify_auth_status();

    return 0;
}

static int auth_status_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t status = ble_auth_get_status_byte();
    int rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
    if (rc != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    ESP_LOGI(TAG, "Auth status read: %s",
             status ? "authenticated" : "not authenticated");

    return 0;
}

static int auth_key_clear_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (!ble_auth_is_authenticated()) {
        ESP_LOGW(TAG, "Key clear rejected - not authenticated");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    esp_err_t err = ble_auth_clear_key();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear auth key");
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI(TAG, "Auth key cleared - device now in first-pairing mode");

    // Notify auth status change
    ble_gatt_notify_auth_status();

    return 0;
}

static int file_list_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (!ble_auth_is_authenticated()) {
        ESP_LOGW(TAG, "File list rejected - not authenticated");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Trigger async file listing via notifications
        ble_gatt_send_file_list(conn_handle);
        // Return empty for initial read
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int file_delete_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (!ble_auth_is_authenticated()) {
        ESP_LOGW(TAG, "File delete rejected - not authenticated");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > 127) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    char path[128] = {0};
    int rc = ble_hs_mbuf_to_flat(ctxt->om, path, len, NULL);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    path[len] = '\0';

    // Build full path if not already absolute
    char full_path[256];
    if (path[0] == '/') {
        strncpy(full_path, path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    } else {
        snprintf(full_path, sizeof(full_path), "/Storage/%s", path);
    }

    ESP_LOGI(TAG, "Deleting file: %s", full_path);

    if (unlink(full_path) != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s", full_path);
        return BLE_ATT_ERR_UNLIKELY;
    }

    // Rescan playlist
    playlist_rescan();

    ESP_LOGI(TAG, "File deleted successfully: %s", full_path);
    return 0;
}

static int transfer_ctrl_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (!ble_auth_is_authenticated()) {
        ESP_LOGW(TAG, "Transfer ctrl rejected - not authenticated");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t buf[256];
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, NULL);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t opcode = buf[0];

    if (opcode == BLE_TRANSFER_OP_CANCEL) {
        ble_transfer_cancel();
    } else if (opcode == BLE_TRANSFER_OP_UPLOAD) {
        // Upload: [0x01][size:4][filename\0]
        if (len < 6) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint32_t size = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);
        char *filename = (char *)&buf[5];
        filename[len - 5] = '\0';  // Ensure null termination

        ble_transfer_start_upload(filename, size, conn_handle);
    } else if (opcode == BLE_TRANSFER_OP_DOWNLOAD) {
        // Download: [0x02][filename\0]
        if (len < 2) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        char *filename = (char *)&buf[1];
        filename[len - 1] = '\0';

        ble_transfer_start_download(filename, conn_handle);
    } else {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int transfer_data_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (!ble_auth_is_authenticated()) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Download: app reads chunk data
        const uint8_t *chunk_data;
        size_t chunk_len;

        esp_err_t err = ble_transfer_get_chunk_data(&chunk_data, &chunk_len);
        if (err != ESP_OK) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        int rc = os_mbuf_append(ctxt->om, chunk_data, chunk_len);
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        // Mark chunk as read and prepare next (will notify when ready)
        ble_transfer_chunk_read_complete();

        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // Upload: app writes chunk data
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

        if (len == 0 || len > BLE_TRANSFER_CHUNK_SIZE) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint8_t data[BLE_TRANSFER_CHUNK_SIZE + 1];
        int rc = ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        esp_err_t err = ble_transfer_receive_chunk(data, len);
        if (err != ESP_OK) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int transfer_progress_access(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (!ble_auth_is_authenticated()) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint32_t transferred = ble_transfer_get_progress();
    uint32_t total = ble_transfer_get_total();

    // Format: [transferred:4][total:4]
    uint8_t data[8];
    data[0] = (transferred >> 0) & 0xFF;
    data[1] = (transferred >> 8) & 0xFF;
    data[2] = (transferred >> 16) & 0xFF;
    data[3] = (transferred >> 24) & 0xFF;
    data[4] = (total >> 0) & 0xFF;
    data[5] = (total >> 8) & 0xFF;
    data[6] = (total >> 16) & 0xFF;
    data[7] = (total >> 24) & 0xFF;

    int rc = os_mbuf_append(ctxt->om, data, sizeof(data));
    if (rc != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return 0;
}

// ============ Public Functions ============

int ble_gatt_svr_init(void) {
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Initialize standard Battery Service (UUID 0x180F)
    ble_svc_bas_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add services failed: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "GATT server initialized");
    return 0;
}

void ble_gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered service: %s, handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "Registered characteristic: %s, def_handle=%d, val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle,
                 ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "Registered descriptor: %s, handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    default:
        break;
    }
}

// File listing callback context
typedef struct {
    uint16_t conn_handle;
    int count;
} file_list_ctx_t;

static void file_list_callback(const char *file_path, void *user_data) {
    file_list_ctx_t *ctx = (file_list_ctx_t *)user_data;

    struct stat st;
    if (stat(file_path, &st) != 0) {
        return;
    }

    // Extract just the filename from full path
    const char *filename = strrchr(file_path, '/');
    filename = filename ? filename + 1 : file_path;

    // Format: [type:1][size:4][filename\0]
    size_t name_len = strlen(filename) + 1;  // Include null terminator
    size_t entry_len = 1 + 4 + name_len;
    uint8_t *entry = malloc(entry_len);
    if (!entry) {
        return;
    }

    entry[0] = S_ISDIR(st.st_mode) ? BLE_FILE_TYPE_DIRECTORY : BLE_FILE_TYPE_FILE;
    entry[1] = (st.st_size >> 0) & 0xFF;
    entry[2] = (st.st_size >> 8) & 0xFF;
    entry[3] = (st.st_size >> 16) & 0xFF;
    entry[4] = (st.st_size >> 24) & 0xFF;
    memcpy(&entry[5], filename, name_len);

    // Send notification
    struct os_mbuf *om = ble_hs_mbuf_from_flat(entry, entry_len);
    if (om) {
        ble_gatts_notify_custom(ctx->conn_handle, file_list_handle, om);
    }

    free(entry);
    ctx->count++;
}

esp_err_t ble_gatt_send_file_list(uint16_t conn_handle) {
    file_list_ctx_t ctx = {
        .conn_handle = conn_handle,
        .count = 0
    };

    // Get base path
    char base_path[32];
    get_base_path(base_path, sizeof(base_path));

    // Scan files and send notifications
    storage_scan_audio_files(file_list_callback, &ctx);

    // Send end marker
    uint8_t end_marker[5] = {BLE_FILE_TYPE_END, 0, 0, 0, 0};
    struct os_mbuf *om = ble_hs_mbuf_from_flat(end_marker, sizeof(end_marker));
    if (om) {
        ble_gatts_notify_custom(conn_handle, file_list_handle, om);
    }

    ESP_LOGI(TAG, "File list sent: %d files", ctx.count);
    return ESP_OK;
}

uint16_t ble_gatt_get_conn_handle(void) {
    return current_conn_handle;
}

void ble_gatt_set_conn_handle(uint16_t conn_handle) {
    current_conn_handle = conn_handle;

    // Update transfer module with handles
    ble_transfer_set_handles(transfer_ctrl_handle, transfer_data_handle,
                              transfer_progress_handle);
}

void ble_gatt_notify_auth_status(void) {
    if (current_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    uint8_t status = ble_auth_get_status_byte();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&status, 1);
    if (om) {
        ble_gatts_notify_custom(current_conn_handle, auth_status_handle, om);
    }
}

void ble_gatt_update_battery_level(uint8_t level) {
    // Update the standard Battery Service level (0-100%)
    ble_svc_bas_battery_level_set(level);
}
