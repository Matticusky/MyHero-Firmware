#include <stdio.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "Storage/storage.h"
#include "Power/power.h"
#include "Buttons/buttons.h"
#include "Indicator/indicator.h"
#include "Audio/audio.h"
#include "Volume/volume.h"
#include "Playlist/playlist.h"
#include "BLE/ble.h"
#include "Debug/debug_server.h"

static const char *TAG = "Firmware";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting firmware application...");

    // Initialize NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Flash initialized successfully.");

    // Initialize storage
    mount_storage();

    // DEBUG: Delete all files on startup (remove for production)
    // storage_delete_all_files();

    // Initialize volume control and restore from NVS
    volume_init();
    volume_load_from_nvs();

    // Initialize playlist (scans storage for audio files)
    playlist_init();
    ESP_LOGI(TAG, "Playlist initialized with %d tracks", playlist_get_count());

    // Initialize power measurement
    init_power_measurement();

    // Initialize buttons
    start_button_scanning_task();

    // Set up LED indicator (starts in IDLE mode)
    init_led_indicator();

    // Initialize audio system
    init_audio_system();

    // Register button callbacks with new handlers
    ESP_LOGI(TAG, "Registering button callbacks...");

    // Play/Pause button
    set_play_pause_button_single_press_callback(play_pause_single_handler);
    set_play_pause_button_double_press_callback(play_pause_double_handler);

    // Record button
    set_esp_record_ctrl_button_single_press_callback(record_single_handler);
    set_esp_record_ctrl_button_double_press_callback(record_double_handler);
    set_esp_record_ctrl_button_long_press_callback(ble_button_handler);

    ESP_LOGI(TAG, "Firmware application started successfully.");

    // DEBUG: Start WiFi debug server (remove for production)
    debug_server_start();

    // Main loop - periodic status logging
    while (1)
    {
        // Log storage info
        print_storage_info();
        vTaskDelay(pdMS_TO_TICKS(10000));

        // Log power status
        bool charging = is_battery_charging();
        bool power_detected = is_power_detected();
        uint16_t vbat_voltage = get_bat_voltage();
        ESP_LOGI(TAG, "Battery Charging: %s", charging ? "Yes" : "No");
        ESP_LOGI(TAG, "Power Detected: %s", power_detected ? "Yes" : "No");
        ESP_LOGI(TAG, "VBAT Voltage: %d mV", vbat_voltage);

        // Log audio state
        ESP_LOGI(TAG, "Audio state: %d, Playlist: %d/%d",
                 audio_get_state(),
                 playlist_get_current_index() + 1,
                 playlist_get_count());

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
