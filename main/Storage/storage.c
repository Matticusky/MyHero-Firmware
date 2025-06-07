#include <stdio.h>
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