#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <soc/spi_pins.h>
#include <esp_vfs_fat_nand.h>
#include <driver/gpio.h>

#include "storage.h"

static const char *TAG = "Storage : ";
static const char *base_path ="/Storage";

#define NAND_SPI_CS_PIN GPIO_NUM_37
#define NAND_SPI_MISO_PIN GPIO_NUM_38
#define NAND_SPI_WP_PIN GPIO_NUM_39
#define NAND_SPI_MOSI_PIN GPIO_NUM_40
#define NAND_SPI_CLK_PIN GPIO_NUM_41
#define NAND_SPI_HD_PIN GPIO_NUM_42

bool is_nand_flash_initialized = false;

void init_nand_flash(spi_nand_flash_device_t **out_handle, spi_device_handle_t *out_spi_handle) {
    ESP_LOGI(TAG, "Initializing NAND flash...");
    if (is_nand_flash_initialized) {
        ESP_LOGW(TAG, "NAND flash is already initialized.");
        return;
    }
    const spi_bus_config_t buscfg = {
        .miso_io_num = NAND_SPI_MISO_PIN,
        .mosi_io_num = NAND_SPI_MOSI_PIN,
        .sclk_io_num = NAND_SPI_CLK_PIN,
        .quadwp_io_num = NAND_SPI_WP_PIN,
        .quadhd_io_num = NAND_SPI_HD_PIN,
        .max_transfer_sz = 16 * 1024, // 16KB
    };

    // initialize spi bus
    ESP_LOGI(TAG, "DMA Channel: %d", SPI_DMA_CH_AUTO);
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully.");

    // add spi device
    const uint32_t spi_flags = SPI_DEVICE_HALFDUPLEX;
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000, // 40MHz
        .mode = 0, // SPI mode
        .spics_io_num = NAND_SPI_CS_PIN,
        .queue_size = 10, // Queue size for transactions
        .flags = spi_flags,
    };

    spi_device_handle_t spi_handle;
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return;
    }
    ESP_LOGI(TAG, "SPI device added successfully.");

    // Initialize NAND flash
    spi_nand_flash_config_t nand_config = {
      .device_handle = spi_handle,
      .io_mode = SPI_NAND_IO_MODE_QIO,
       .flags = spi_flags
    };
    spi_nand_flash_device_t *nand_device_handle;
    ret = spi_nand_flash_init_device(&nand_config, &nand_device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NAND flash: %s", esp_err_to_name(ret));
        spi_bus_remove_device(spi_handle);
        spi_bus_free(SPI2_HOST);
        return;
    }
    ESP_LOGI(TAG, "NAND flash initialized successfully.");
    is_nand_flash_initialized = true;
    *out_handle = nand_device_handle;
    *out_spi_handle = spi_handle;
}


void deinit_nand_flash(spi_nand_flash_device_t *nand_device_handle, spi_device_handle_t spi_handle) {
    if (!is_nand_flash_initialized) {
        ESP_LOGW(TAG, "NAND flash is not initialized.");
        return;
    }
    ESP_LOGI(TAG, "Deinitializing NAND flash...");
    esp_err_t ret = spi_nand_flash_deinit_device(nand_device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize NAND flash: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "NAND flash deinitialized successfully.");
    }
    ret = spi_bus_remove_device(spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove SPI device: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPI device removed successfully.");
    }
    ret = spi_bus_free(SPI2_HOST);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to free SPI bus: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPI bus freed successfully.");
    }
    is_nand_flash_initialized = false;
}

void mount_storage() {
    ESP_LOGI(TAG, "Mounting storage...");
    esp_err_t ret;
    spi_nand_flash_device_t *nand_device_handle = NULL;
    spi_device_handle_t spi_handle = NULL;

    init_nand_flash(&nand_device_handle, &spi_handle);
    if (!is_nand_flash_initialized) {
        ESP_LOGE(TAG, "Failed to initialize NAND flash.");
        return;
    }
    esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 5, // Maximum number of open files
        .format_if_mount_failed = true, // Format if mount fails
        .allocation_unit_size = 16*1024, // Allocation unit size
    };

    // Mount the NAND flash
    ret = esp_vfs_fat_nand_mount(base_path, nand_device_handle, &mount_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount NAND flash: %s", esp_err_to_name(ret));
        deinit_nand_flash(nand_device_handle, spi_handle);
        return;
    }
    ESP_LOGI(TAG, "Storage mounted successfully at %s", base_path);
}

void get_storage_info(size_t *total_size, size_t *used_size, size_t *free_size) {
    uint64_t bytes_total, bytes_free;
    esp_vfs_fat_info(base_path, &bytes_total, &bytes_free);
    if (total_size) {
        *total_size = bytes_total;
    }
    if (used_size) {
        *used_size = bytes_total - bytes_free;
    }
    if (free_size) {
        *free_size = bytes_free;
    }
}

void print_storage_info() {
    size_t total_size, used_size, free_size;
    get_storage_info(&total_size, &used_size, &free_size);
    ESP_LOGI(TAG, "Storage Info:");
    ESP_LOGI(TAG, "Total Size: %zu bytes", total_size);
    ESP_LOGI(TAG, "Used Size: %zu bytes", used_size);
    ESP_LOGI(TAG, "Free Size: %zu bytes", free_size);
}

void get_base_path(char *path, size_t size) {
    if (path == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid path or size");
        return;
    }
    if (size < (strlen(base_path) + 1)) {
        ESP_LOGE(TAG, "Path buffer is too small");
        return;
    }
    snprintf(path, size, "%s", base_path);
    ESP_LOGI(TAG, "Base path: %s", path);
}

esp_err_t storage_scan_audio_files(storage_scan_cb_t callback, void *user_data) {
    if (!callback) {
        ESP_LOGE(TAG, "Invalid callback");
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(base_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", base_path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    char full_path[300];
    int file_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip directories
        if (entry->d_type == DT_DIR) {
            continue;
        }

        // Check for .wav extension (case insensitive)
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && (strcasecmp(ext, ".wav") == 0)) {
            snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);
            callback(full_path, user_data);
            file_count++;
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Scanned %d audio files", file_count);
    return ESP_OK;
}

esp_err_t storage_generate_recording_path(char *path_buf, size_t buf_size) {
    if (!path_buf || buf_size < 32) {
        ESP_LOGE(TAG, "Invalid buffer");
        return ESP_ERR_INVALID_ARG;
    }

    int max_num = 0;

    DIR *dir = opendir(base_path);
    if (!dir) {
        // Directory doesn't exist or error - start at 0001
        snprintf(path_buf, buf_size, "%s/recording_0001.wav", base_path);
        ESP_LOGI(TAG, "Generated recording path: %s", path_buf);
        return ESP_OK;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        int num;
        // Match pattern: recording_NNNN.wav
        if (sscanf(entry->d_name, "recording_%d.wav", &num) == 1) {
            if (num > max_num) {
                max_num = num;
            }
        }
    }
    closedir(dir);

    snprintf(path_buf, buf_size, "%s/recording_%04d.wav", base_path, max_num + 1);
    ESP_LOGI(TAG, "Generated recording path: %s", path_buf);
    return ESP_OK;
}

bool storage_file_exists(const char *path) {
    if (!path) {
        return false;
    }

    struct stat st;
    return (stat(path, &st) == 0);
}

esp_err_t storage_delete_all_files(void) {
    ESP_LOGW(TAG, "Deleting all files in storage...");

    DIR *dir = opendir(base_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", base_path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    char full_path[300];
    int deleted_count = 0;
    int failed_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip directories (including . and ..)
        if (entry->d_type == DT_DIR) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

        if (unlink(full_path) == 0) {
            ESP_LOGI(TAG, "Deleted: %s", entry->d_name);
            deleted_count++;
        } else {
            ESP_LOGE(TAG, "Failed to delete: %s", entry->d_name);
            failed_count++;
        }
    }

    closedir(dir);

    ESP_LOGW(TAG, "Deleted %d files, %d failed", deleted_count, failed_count);
    return (failed_count == 0) ? ESP_OK : ESP_FAIL;
}