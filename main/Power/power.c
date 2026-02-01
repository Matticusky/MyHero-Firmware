#include <stdio.h>
#include <stdbool.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include "esp_adc/adc_cali_scheme.h"
#include "Power/power.h"

#define power_detected_pin GPIO_NUM_11
#define charge_detected_pin GPIO_NUM_12
#define vbat_measure_pin GPIO_NUM_8
#define vbat_measure_channel ADC_CHANNEL_7

static const char *TAG = "Power : ";

adc_cali_handle_t adc_cali_handle = NULL;
adc_oneshot_unit_handle_t adc1_handle;

SemaphoreHandle_t power_semaphore = NULL;

bool acquire_power_semaphore(){
    if (power_semaphore == NULL) {
        ESP_LOGE(TAG, "ADC semaphore is not initialized.");
        return false;
    }
    if (xSemaphoreTake(power_semaphore, portMAX_DELAY) == pdTRUE) {
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to acquire ADC semaphore.");
        return false;
    }
}
void release_power_semaphore(){
    if (power_semaphore == NULL) {
        ESP_LOGE(TAG, "ADC semaphore is not initialized.");
        return;
    }
    if (xSemaphoreGive(power_semaphore) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to release ADC semaphore.");
    }
}

void init_vbat_measurement(){
    ESP_LOGI(TAG, "Initializing VBAT measurement...");
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ret = adc_oneshot_config_channel(adc1_handle, vbat_measure_channel, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc1_handle);
        return;
    }
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC calibration scheme: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc1_handle);
        return;
    }
}

void init_power_measurement(){
    ESP_LOGI(TAG, "Initializing power measurement...");
    // Initialize GPIO pins for power detection
    gpio_reset_pin(power_detected_pin);
    gpio_set_direction(power_detected_pin, GPIO_MODE_INPUT);
    gpio_reset_pin(charge_detected_pin);
    gpio_set_direction(charge_detected_pin, GPIO_MODE_INPUT);
    init_vbat_measurement();
    // initialize semaphore for ADC access
    power_semaphore = xSemaphoreCreateMutex();
    if (power_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create ADC semaphore.");
        return;
    }
    xSemaphoreGive(power_semaphore);
    ESP_LOGI(TAG, "Power measurement initialized successfully.");
}

bool is_battery_charging(){
    acquire_power_semaphore();
    bool charging = gpio_get_level(charge_detected_pin) == 0;
    release_power_semaphore();
    return charging;
}

bool is_power_detected(){
    acquire_power_semaphore();
    bool power_detected = (gpio_get_level(power_detected_pin) == 0 || gpio_get_level(charge_detected_pin) == 0);
    release_power_semaphore();
    return power_detected;
}

uint16_t get_bat_voltage(){
    if (adc_cali_handle == NULL) {
        ESP_LOGE(TAG, "ADC calibration handle is not initialized.");
        return 0;
    }
    acquire_power_semaphore();
    int voltage_raw[5];
    uint16_t voltage;
    esp_err_t ret;
    // ret = adc_oneshot_read(adc1_handle, vbat_measure_channel, &voltage_raw);
    for (int i = 0; i < 5; i++) {
        ret = adc_oneshot_read(adc1_handle, vbat_measure_channel, &voltage_raw[i]);
        ret|= adc_cali_raw_to_voltage(adc_cali_handle, voltage_raw[i], &voltage_raw[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ADC value: %s", esp_err_to_name(ret));
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Delay for stability
    }
    release_power_semaphore();
    voltage = (voltage_raw[0] + voltage_raw[1] + voltage_raw[2] + voltage_raw[3] + voltage_raw[4]) / 5;
    return voltage*2;
}

uint8_t get_battery_percent(void) {
    uint16_t voltage_mv = get_bat_voltage();

    // Clamp to valid range
    if (voltage_mv >= BATTERY_VOLTAGE_FULL) {
        return 100;
    }
    if (voltage_mv <= BATTERY_VOLTAGE_EMPTY) {
        return 0;
    }

    // Linear interpolation between empty and full
    // percent = (voltage - empty) / (full - empty) * 100
    uint32_t range = BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY;
    uint32_t offset = voltage_mv - BATTERY_VOLTAGE_EMPTY;
    uint8_t percent = (uint8_t)((offset * 100) / range);

    return percent;
}


