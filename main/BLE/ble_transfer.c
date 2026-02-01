#include "ble_transfer.h"
#include "ble_uuids.h"
#include "ble_auth.h"
#include "../Playlist/playlist.h"
#include "../Indicator/indicator.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>
#include <mbedtls/base64.h>
#include <host/ble_hs.h>

static const char *TAG = "BLE_TRANSFER";

// Transfer context
typedef struct {
    ble_xfer_state_t state;
    ble_xfer_dir_t direction;
    char file_path[128];
    uint32_t total_bytes;
    uint32_t transferred_bytes;
    FILE *file_handle;
    uint16_t conn_handle;
    bool delete_on_error;  // For uploads, delete partial file on error
} ble_transfer_ctx_t;

static ble_transfer_ctx_t ctx = {
    .state = BLE_XFER_STATE_IDLE,
    .direction = BLE_XFER_DIR_NONE,
    .file_handle = NULL,
};

// Characteristic handles for notifications
static uint16_t ctrl_attr_handle = 0;
static uint16_t data_attr_handle = 0;
static uint16_t progress_attr_handle = 0;

// Forward declarations
static void notify_status(uint8_t status, uint32_t size);
static void notify_progress(void);
static void cleanup_transfer(bool success);

void ble_transfer_init(void) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = BLE_XFER_STATE_IDLE;
    ctx.direction = BLE_XFER_DIR_NONE;
    ESP_LOGI(TAG, "Transfer module initialized");
}

void ble_transfer_set_handles(uint16_t ctrl_handle, uint16_t data_handle,
                               uint16_t progress_handle) {
    ctrl_attr_handle = ctrl_handle;
    data_attr_handle = data_handle;
    progress_attr_handle = progress_handle;
    ESP_LOGI(TAG, "Handles set: ctrl=%d, data=%d, progress=%d",
             ctrl_handle, data_handle, progress_handle);
}

esp_err_t ble_transfer_start_upload(const char *filename, uint32_t total_size,
                                     uint16_t conn_handle) {
    if (!ble_auth_is_authenticated()) {
        ESP_LOGW(TAG, "Upload rejected - not authenticated");
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx.state != BLE_XFER_STATE_IDLE) {
        ESP_LOGW(TAG, "Upload rejected - transfer already in progress");
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_ERR_INVALID_STATE;
    }

    if (!filename || total_size == 0) {
        ESP_LOGE(TAG, "Invalid upload parameters");
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_ERR_INVALID_ARG;
    }

    // Build full path
    snprintf(ctx.file_path, sizeof(ctx.file_path), "/Storage/%s", filename);

    // Open file for writing
    ctx.file_handle = fopen(ctx.file_path, "wb");
    if (!ctx.file_handle) {
        ESP_LOGE(TAG, "Failed to create file: %s", ctx.file_path);
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_FAIL;
    }

    ctx.state = BLE_XFER_STATE_UPLOAD_PENDING;
    ctx.direction = BLE_XFER_DIR_UPLOAD;
    ctx.total_bytes = total_size;
    ctx.transferred_bytes = 0;
    ctx.conn_handle = conn_handle;
    ctx.delete_on_error = true;

    ESP_LOGI(TAG, "Upload started: %s (%lu bytes)", ctx.file_path,
             (unsigned long)total_size);

    // Set LED to transfer mode
    led_set_mode(LED_MODE_BLE_TRANSFER);

    // Notify ready
    notify_status(BLE_TRANSFER_STATUS_READY, 0);

    return ESP_OK;
}

esp_err_t ble_transfer_start_download(const char *filename, uint16_t conn_handle) {
    if (!ble_auth_is_authenticated()) {
        ESP_LOGW(TAG, "Download rejected - not authenticated");
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx.state != BLE_XFER_STATE_IDLE) {
        ESP_LOGW(TAG, "Download rejected - transfer already in progress");
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_ERR_INVALID_STATE;
    }

    if (!filename) {
        ESP_LOGE(TAG, "Invalid download parameters");
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_ERR_INVALID_ARG;
    }

    // Build full path
    snprintf(ctx.file_path, sizeof(ctx.file_path), "/Storage/%s", filename);

    // Get file size
    struct stat st;
    if (stat(ctx.file_path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", ctx.file_path);
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_ERR_NOT_FOUND;
    }

    // Open file for reading
    ctx.file_handle = fopen(ctx.file_path, "rb");
    if (!ctx.file_handle) {
        ESP_LOGE(TAG, "Failed to open file: %s", ctx.file_path);
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_FAIL;
    }

    ctx.state = BLE_XFER_STATE_DOWNLOAD_PENDING;
    ctx.direction = BLE_XFER_DIR_DOWNLOAD;
    ctx.total_bytes = st.st_size;
    ctx.transferred_bytes = 0;
    ctx.conn_handle = conn_handle;
    ctx.delete_on_error = false;

    ESP_LOGI(TAG, "Download started: %s (%lu bytes)", ctx.file_path,
             (unsigned long)ctx.total_bytes);

    // Set LED to transfer mode
    led_set_mode(LED_MODE_BLE_TRANSFER);

    // Notify ready with file size
    notify_status(BLE_TRANSFER_STATUS_READY, ctx.total_bytes);

    return ESP_OK;
}

esp_err_t ble_transfer_receive_chunk(const uint8_t *data, size_t len) {
    if (ctx.state != BLE_XFER_STATE_UPLOAD_PENDING &&
        ctx.state != BLE_XFER_STATE_UPLOADING) {
        ESP_LOGW(TAG, "Unexpected data chunk - not in upload state");
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || len == 0) {
        ESP_LOGE(TAG, "Invalid chunk data");
        return ESP_ERR_INVALID_ARG;
    }

    ctx.state = BLE_XFER_STATE_UPLOADING;

    // Decode base64
    uint8_t decoded[BLE_TRANSFER_CHUNK_RAW_SIZE + 1];
    size_t decoded_len = 0;

    int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len,
                                     data, len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d", ret);
        cleanup_transfer(false);
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_FAIL;
    }

    // Write to file
    size_t written = fwrite(decoded, 1, decoded_len, ctx.file_handle);
    if (written != decoded_len) {
        ESP_LOGE(TAG, "Write failed: wrote %d of %d bytes",
                 (int)written, (int)decoded_len);
        cleanup_transfer(false);
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_FAIL;
    }

    ctx.transferred_bytes += decoded_len;

    // Notify progress
    notify_progress();

    ESP_LOGD(TAG, "Received chunk: %d bytes, progress: %lu/%lu",
             (int)decoded_len, (unsigned long)ctx.transferred_bytes,
             (unsigned long)ctx.total_bytes);

    // Check if complete
    if (ctx.transferred_bytes >= ctx.total_bytes) {
        fflush(ctx.file_handle);
        fclose(ctx.file_handle);
        ctx.file_handle = NULL;

        // Verify actual file size
        struct stat st;
        if (stat(ctx.file_path, &st) == 0 &&
            (uint32_t)st.st_size == ctx.total_bytes) {
            ctx.state = BLE_XFER_STATE_COMPLETE;
            ESP_LOGI(TAG, "Upload complete: %s", ctx.file_path);
            notify_status(BLE_TRANSFER_STATUS_COMPLETE, 0);

            // Rescan playlist for new audio files
            playlist_rescan();

            // Restore LED mode
            led_set_mode(LED_MODE_BLE_PAIRING);
        } else {
            ESP_LOGE(TAG, "Upload size mismatch: expected %lu, got %lu",
                     (unsigned long)ctx.total_bytes,
                     (unsigned long)(st.st_size));
            // Delete partial/corrupt file
            unlink(ctx.file_path);
            ctx.state = BLE_XFER_STATE_ERROR;
            notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
            led_set_mode(LED_MODE_BLE_PAIRING);
        }
        ctx.delete_on_error = false;
    } else {
        // Ready for next chunk
        notify_status(BLE_TRANSFER_STATUS_READY, 0);
    }

    return ESP_OK;
}

esp_err_t ble_transfer_send_next_chunk(void) {
    if (ctx.state != BLE_XFER_STATE_DOWNLOAD_PENDING &&
        ctx.state != BLE_XFER_STATE_DOWNLOADING) {
        return ESP_ERR_INVALID_STATE;
    }

    ctx.state = BLE_XFER_STATE_DOWNLOADING;

    // Read raw data from file
    uint8_t raw_buffer[BLE_TRANSFER_CHUNK_RAW_SIZE];
    size_t read_len = fread(raw_buffer, 1, sizeof(raw_buffer), ctx.file_handle);

    if (read_len == 0) {
        // EOF - transfer complete
        fclose(ctx.file_handle);
        ctx.file_handle = NULL;
        ctx.state = BLE_XFER_STATE_COMPLETE;

        ESP_LOGI(TAG, "Download complete: %s", ctx.file_path);
        notify_status(BLE_TRANSFER_STATUS_COMPLETE, 0);

        // Restore LED mode
        led_set_mode(LED_MODE_BLE_PAIRING);

        return ESP_ERR_NOT_FINISHED;
    }

    // Base64 encode
    uint8_t encoded[BLE_TRANSFER_CHUNK_ENC_SIZE + 1];
    size_t encoded_len = 0;

    int ret = mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_len,
                                     raw_buffer, read_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encode failed: %d", ret);
        cleanup_transfer(false);
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_FAIL;
    }

    // Send via notification on data characteristic
    if (data_attr_handle != 0 && ctx.conn_handle != 0) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(encoded, encoded_len);
        if (om) {
            int rc = ble_gatts_notify_custom(ctx.conn_handle, data_attr_handle, om);
            if (rc != 0) {
                ESP_LOGW(TAG, "Failed to send data notification: %d", rc);
            }
        }
    }

    ctx.transferred_bytes += read_len;
    notify_progress();

    ESP_LOGD(TAG, "Sent chunk: %d bytes, progress: %lu/%lu",
             (int)read_len, (unsigned long)ctx.transferred_bytes,
             (unsigned long)ctx.total_bytes);

    return ESP_OK;
}

void ble_transfer_cancel(void) {
    if (ctx.state == BLE_XFER_STATE_IDLE) {
        return;
    }

    ESP_LOGI(TAG, "Transfer cancelled");
    cleanup_transfer(false);

    // Restore LED mode
    led_set_mode(LED_MODE_BLE_PAIRING);
}

static void cleanup_transfer(bool success) {
    if (ctx.file_handle) {
        fclose(ctx.file_handle);
        ctx.file_handle = NULL;
    }

    // Delete partial uploads on error
    if (!success && ctx.delete_on_error && ctx.file_path[0] != '\0') {
        ESP_LOGW(TAG, "Deleting partial upload: %s", ctx.file_path);
        unlink(ctx.file_path);
    }

    ctx.state = success ? BLE_XFER_STATE_COMPLETE : BLE_XFER_STATE_ERROR;
    ctx.direction = BLE_XFER_DIR_NONE;
    ctx.delete_on_error = false;
}

static void notify_status(uint8_t status, uint32_t size) {
    if (ctrl_attr_handle == 0 || ctx.conn_handle == 0) {
        return;
    }

    // Format: [status:1][size:4]
    uint8_t response[5];
    response[0] = status;
    response[1] = (size >> 0) & 0xFF;
    response[2] = (size >> 8) & 0xFF;
    response[3] = (size >> 16) & 0xFF;
    response[4] = (size >> 24) & 0xFF;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(response, sizeof(response));
    if (om) {
        int rc = ble_gatts_notify_custom(ctx.conn_handle, ctrl_attr_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "Failed to send status notification: %d", rc);
        }
    }
}

static void notify_progress(void) {
    if (progress_attr_handle == 0 || ctx.conn_handle == 0) {
        return;
    }

    // Format: [transferred:4][total:4]
    uint8_t data[8];
    data[0] = (ctx.transferred_bytes >> 0) & 0xFF;
    data[1] = (ctx.transferred_bytes >> 8) & 0xFF;
    data[2] = (ctx.transferred_bytes >> 16) & 0xFF;
    data[3] = (ctx.transferred_bytes >> 24) & 0xFF;
    data[4] = (ctx.total_bytes >> 0) & 0xFF;
    data[5] = (ctx.total_bytes >> 8) & 0xFF;
    data[6] = (ctx.total_bytes >> 16) & 0xFF;
    data[7] = (ctx.total_bytes >> 24) & 0xFF;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, sizeof(data));
    if (om) {
        int rc = ble_gatts_notify_custom(ctx.conn_handle, progress_attr_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "Failed to send progress notification: %d", rc);
        }
    }
}

ble_xfer_state_t ble_transfer_get_state(void) {
    return ctx.state;
}

ble_xfer_dir_t ble_transfer_get_direction(void) {
    return ctx.direction;
}

uint32_t ble_transfer_get_progress(void) {
    return ctx.transferred_bytes;
}

uint32_t ble_transfer_get_total(void) {
    return ctx.total_bytes;
}

uint8_t ble_transfer_get_percent(void) {
    if (ctx.total_bytes == 0) {
        return 0;
    }
    return (uint8_t)((ctx.transferred_bytes * 100) / ctx.total_bytes);
}

bool ble_transfer_is_active(void) {
    return ctx.state == BLE_XFER_STATE_UPLOAD_PENDING ||
           ctx.state == BLE_XFER_STATE_UPLOADING ||
           ctx.state == BLE_XFER_STATE_DOWNLOAD_PENDING ||
           ctx.state == BLE_XFER_STATE_DOWNLOADING;
}

uint32_t ble_transfer_get_file_size(void) {
    return ctx.total_bytes;
}
