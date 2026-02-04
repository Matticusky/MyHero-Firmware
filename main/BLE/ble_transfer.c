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
#include <esp_timer.h>
#include <host/ble_hs.h>

static const char *TAG = "BLE_TRANSFER";

// Timer delay for deferred notifications (microseconds)
// Reduced from 1000us to 100us for faster throughput
#define NOTIFY_TIMER_DELAY_US  100

// Timer for deferred notifications (can't send from GATT callback context)
static esp_timer_handle_t notify_timer = NULL;
typedef enum {
    DEFERRED_NONE = 0,
    DEFERRED_CHUNK_READY,
    DEFERRED_COMPLETE,
    DEFERRED_ERROR,
} deferred_action_t;
static deferred_action_t deferred_action = DEFERRED_NONE;
static uint32_t deferred_size = 0;

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
    // Download chunk buffer (for read-based flow) - raw binary, no base64
    uint8_t chunk_buffer[BLE_TRANSFER_CHUNK_SIZE];
    size_t chunk_len;
    bool chunk_ready;
} ble_transfer_ctx_t;

static ble_transfer_ctx_t ctx = {
    .state = BLE_XFER_STATE_IDLE,
    .direction = BLE_XFER_DIR_NONE,
    .file_handle = NULL,
    .chunk_ready = false,
};

// Characteristic handles for notifications
static uint16_t ctrl_attr_handle = 0;
static uint16_t data_attr_handle = 0;
static uint16_t progress_attr_handle = 0;

// Forward declarations
static void notify_status(uint8_t status, uint32_t size);
static void notify_data_ready(uint32_t size);
static void notify_progress(void);
static void cleanup_transfer(bool success);

// Timer callback for deferred notifications
static void deferred_notify_callback(void *arg) {
    switch (deferred_action) {
        case DEFERRED_CHUNK_READY:
            notify_data_ready(deferred_size);
            notify_progress();
            break;
        case DEFERRED_COMPLETE:
            ESP_LOGI(TAG, "Download complete");
            notify_status(BLE_TRANSFER_STATUS_COMPLETE, 0);
            led_set_mode(LED_MODE_BLE_PAIRING);
            // Reset state to allow new transfers
            ctx.state = BLE_XFER_STATE_IDLE;
            ctx.direction = BLE_XFER_DIR_NONE;
            break;
        case DEFERRED_ERROR:
            ESP_LOGE(TAG, "Transfer error");
            notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
            led_set_mode(LED_MODE_BLE_PAIRING);
            // Reset state to allow new transfers
            ctx.state = BLE_XFER_STATE_IDLE;
            ctx.direction = BLE_XFER_DIR_NONE;
            break;
        default:
            break;
    }
    deferred_action = DEFERRED_NONE;
}

void ble_transfer_init(void) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = BLE_XFER_STATE_IDLE;
    ctx.direction = BLE_XFER_DIR_NONE;

    // Create one-shot timer for deferred notifications
    if (notify_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = deferred_notify_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "xfer_notify"
        };
        esp_timer_create(&timer_args, &notify_timer);
    }

    ESP_LOGI(TAG, "Transfer module initialized");
}

void ble_transfer_set_handles(uint16_t ctrl_handle, uint16_t data_handle,
                               uint16_t progress_handle) {
    ctrl_attr_handle = ctrl_handle;
    data_attr_handle = data_handle;
    progress_attr_handle = progress_handle;
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

// Forward declaration
static void notify_data_ready(uint32_t size);

esp_err_t ble_transfer_start_download(const char *filename, uint16_t conn_handle) {
    if (!ble_auth_is_authenticated()) {
        ESP_LOGW(TAG, "Download rejected - not authenticated");
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx.state != BLE_XFER_STATE_IDLE) {
        ESP_LOGW(TAG, "Download rejected - transfer in progress");
        notify_data_ready(0);  // Error on data characteristic
        return ESP_ERR_INVALID_STATE;
    }

    if (!filename) {
        ESP_LOGE(TAG, "Download rejected - filename is NULL");
        notify_data_ready(0);
        return ESP_ERR_INVALID_ARG;
    }

    // Build full path
    snprintf(ctx.file_path, sizeof(ctx.file_path), "/Storage/%s", filename);

    // Get file size
    struct stat st;
    if (stat(ctx.file_path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", ctx.file_path);
        notify_data_ready(0);
        return ESP_ERR_NOT_FOUND;
    }

    // Open file for reading
    ctx.file_handle = fopen(ctx.file_path, "rb");
    if (!ctx.file_handle) {
        ESP_LOGE(TAG, "Failed to open file: %s", ctx.file_path);
        notify_data_ready(0);
        return ESP_FAIL;
    }

    ctx.state = BLE_XFER_STATE_DOWNLOAD_PENDING;
    ctx.direction = BLE_XFER_DIR_DOWNLOAD;
    ctx.total_bytes = st.st_size;
    ctx.transferred_bytes = 0;
    ctx.conn_handle = conn_handle;
    ctx.delete_on_error = false;
    ctx.chunk_ready = false;
    ctx.chunk_len = 0;

    ESP_LOGI(TAG, "Download started: %s (%lu bytes)", ctx.file_path,
             (unsigned long)ctx.total_bytes);

    // Set LED to transfer mode
    led_set_mode(LED_MODE_BLE_TRANSFER);

    // Prepare first chunk
    ble_transfer_prepare_next_chunk();

    // Notify on transfer_data: [0x01][filesize:4] to signal ready
    notify_data_ready(ctx.total_bytes);

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

    // Write raw binary data directly to file (no base64 decoding)
    size_t written = fwrite(data, 1, len, ctx.file_handle);
    if (written != len) {
        ESP_LOGE(TAG, "Write failed: wrote %d of %d bytes", (int)written, (int)len);
        cleanup_transfer(false);
        notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
        return ESP_FAIL;
    }

    ctx.transferred_bytes += len;

    // Notify progress
    notify_progress();

    ESP_LOGD(TAG, "Received chunk: %d bytes, progress: %lu/%lu",
             (int)len, (unsigned long)ctx.transferred_bytes,
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
            ESP_LOGI(TAG, "Upload complete: %s", ctx.file_path);
            notify_status(BLE_TRANSFER_STATUS_COMPLETE, 0);

            // Rescan playlist for new audio files
            playlist_rescan();

            // Restore LED mode
            led_set_mode(LED_MODE_BLE_PAIRING);

            // Reset state to allow new transfers
            ctx.state = BLE_XFER_STATE_IDLE;
            ctx.direction = BLE_XFER_DIR_NONE;
        } else {
            ESP_LOGE(TAG, "Upload size mismatch: expected %lu, got %lu",
                     (unsigned long)ctx.total_bytes,
                     (unsigned long)(st.st_size));
            // Delete partial/corrupt file
            unlink(ctx.file_path);
            notify_status(BLE_TRANSFER_STATUS_ERROR, 0);
            led_set_mode(LED_MODE_BLE_PAIRING);

            // Reset state to allow new transfers
            ctx.state = BLE_XFER_STATE_IDLE;
            ctx.direction = BLE_XFER_DIR_NONE;
        }
        ctx.delete_on_error = false;
    } else {
        // Ready for next chunk
        notify_status(BLE_TRANSFER_STATUS_READY, 0);
    }

    return ESP_OK;
}

esp_err_t ble_transfer_prepare_next_chunk(void) {
    if (ctx.state != BLE_XFER_STATE_DOWNLOAD_PENDING &&
        ctx.state != BLE_XFER_STATE_DOWNLOADING) {
        return ESP_ERR_INVALID_STATE;
    }

    ctx.state = BLE_XFER_STATE_DOWNLOADING;

    // Read raw binary data directly into chunk buffer (no base64)
    size_t read_len = fread(ctx.chunk_buffer, 1, BLE_TRANSFER_CHUNK_SIZE, ctx.file_handle);

    if (read_len == 0) {
        // EOF - no more data
        ctx.chunk_ready = false;
        ctx.chunk_len = 0;
        return ESP_ERR_NOT_FINISHED;
    }

    ctx.chunk_len = read_len;
    ctx.chunk_ready = true;
    ctx.transferred_bytes += read_len;

    ESP_LOGD(TAG, "Prepared chunk: %d bytes, progress: %lu/%lu",
             (int)read_len,
             (unsigned long)ctx.transferred_bytes,
             (unsigned long)ctx.total_bytes);

    return ESP_OK;
}

esp_err_t ble_transfer_get_chunk_data(const uint8_t **data, size_t *len) {
    if (!ctx.chunk_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    *data = ctx.chunk_buffer;
    *len = ctx.chunk_len;
    return ESP_OK;
}

void ble_transfer_chunk_read_complete(void) {
    if (ctx.direction != BLE_XFER_DIR_DOWNLOAD) {
        return;
    }

    ctx.chunk_ready = false;

    // Prepare next chunk
    esp_err_t err = ble_transfer_prepare_next_chunk();

    if (err == ESP_ERR_NOT_FINISHED) {
        // Transfer complete
        fclose(ctx.file_handle);
        ctx.file_handle = NULL;
        ctx.state = BLE_XFER_STATE_COMPLETE;

        // Defer notification (can't send from GATT callback context)
        deferred_action = DEFERRED_COMPLETE;
        esp_timer_start_once(notify_timer, NOTIFY_TIMER_DELAY_US);
    } else if (err == ESP_OK) {
        // Defer notification for next chunk ready
        deferred_action = DEFERRED_CHUNK_READY;
        deferred_size = ctx.chunk_len;
        esp_timer_start_once(notify_timer, NOTIFY_TIMER_DELAY_US);
    } else {
        // Error - defer notification
        ESP_LOGE(TAG, "Error preparing chunk");
        cleanup_transfer(false);
        deferred_action = DEFERRED_ERROR;
        esp_timer_start_once(notify_timer, NOTIFY_TIMER_DELAY_US);
    }
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

    // Reset to IDLE to allow new transfers
    ctx.state = BLE_XFER_STATE_IDLE;
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
        ble_gatts_notify_custom(ctx.conn_handle, ctrl_attr_handle, om);
    }
}

static void notify_data_ready(uint32_t size) {
    if (data_attr_handle == 0 || ctx.conn_handle == 0) {
        return;
    }

    // Format: [0x01=ready][size:4] or [0x00=error][0:4]
    uint8_t response[5];
    response[0] = (size > 0) ? BLE_TRANSFER_STATUS_READY : BLE_TRANSFER_STATUS_ERROR;
    response[1] = (size >> 0) & 0xFF;
    response[2] = (size >> 8) & 0xFF;
    response[3] = (size >> 16) & 0xFF;
    response[4] = (size >> 24) & 0xFF;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(response, sizeof(response));
    if (om) {
        ble_gatts_notify_custom(ctx.conn_handle, data_attr_handle, om);
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
        ble_gatts_notify_custom(ctx.conn_handle, progress_attr_handle, om);
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
