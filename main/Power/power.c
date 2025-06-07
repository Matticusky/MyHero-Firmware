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
#include "power.h"

#define power_detected_pin GPIO_PIN_11
#define charge_detected_pin GPIO_PIN_12
#define vbat_measure_pin GPIO_NUM_8
#define vbat_measure_channel ADC_CHANNEL_7

static const char *TAG = "Power : ";

adc_cali_handle_t adc_cali_handle = NULL;

void init_vbat_measurement(){
    ESP_LOGI(TAG, "Initializing VBAT measurement...");
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_init_cfg_t init_config = {
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
    gpio_reset_pin(power_detected_pin);
    gpio_set_direction(power_detected_pin, GPIO_MODE_INPUT);
    gpio_reset_pin(charge_detected_pin);
    gpio_set_direction(charge_detected_pin, GPIO_MODE_INPUT);
}

bool is_battery_charging(){
    return gpio_get_level(charge_detected_pin) == 0;
}

bool is_power_detected(){
    return (gpio_get_level(power_detected_pin) == 0 || gpio_get_level(charge_detected_pin) == 0);
}



