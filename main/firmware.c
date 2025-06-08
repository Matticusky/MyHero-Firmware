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

static const char *TAG = "Firmware";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting firmware application...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Flash initialized successfully.");
    // Initialize storage
    mount_storage();
    // Initialize power measurement
    init_power_measurement();
    // Initialize buttons
    start_button_scanning_task();

    // set up LED indicator
    init_led_indicator();
    set_led_indicator_frequency(1000); // Set frequency to 1 Hz
    set_led_indicator_duty(128); // Set duty cycle to 50%
    start_led_indicator();
    ESP_LOGI(TAG, "Firmware application started successfully.");

    // register callbacks for buttons to start/stop indicator
    ESP_LOGI(TAG, "Registering button callbacks...");
    set_play_pause_button_single_press_callback(play_pause_button_press_handler);
    set_play_pause_button_double_press_callback(start_led_indicator);

    // start audio subsystem
    init_audio_system();
    set_esp_record_ctrl_button_single_press_callback(record_button_press_handler);

    while (1)
    {
        // storage_test
        print_storage_info();
        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay for 5 seconds


        // power_test
        bool charging = is_battery_charging();
        bool power_detected = is_power_detected();
        uint16_t vbat_voltage = get_bat_voltage();
        ESP_LOGI(TAG, "Battery Charging: %s", charging ? "Yes" : "No");
        ESP_LOGI(TAG, "Power Detected: %s", power_detected ? "Yes" : "No");
        ESP_LOGI(TAG, "VBAT Voltage: %d mV", vbat_voltage);
        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay for 5 seconds

        ESP_LOGI(TAG, "Firmware application running...");
    }
    

}
