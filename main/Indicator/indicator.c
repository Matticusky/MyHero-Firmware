#include <stdio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/ledc.h>

#include "Indicator/indicator.h"

#define LED_INDICATOR_PIN 21

static const char *TAG = "Indicator : ";


uint32_t frequency = 1000; // Default frequency in Hz
uint8_t duty_cycle = 0; // Default duty cycle (0-1023 for 10-bit resolution)


void init_led_indicator(){
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000, // 1 kHz
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return;
    }
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LED_INDICATOR_PIN,
        .duty = 0, // Start with duty cycle 0
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return;
    }
}

void set_led_indicator_frequency(uint32_t freq) {
    frequency = freq;
    ESP_LOGI(TAG, "LED indicator frequency set to %lu Hz", frequency);
}

void set_led_indicator_duty(uint8_t duty) {
    duty_cycle = duty;
    ESP_LOGI(TAG, "LED indicator duty cycle set to %d", duty_cycle);
}

void start_led_indicator() {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequency);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_cycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "LED indicator started with frequency %lu Hz and duty cycle %d", frequency, duty_cycle);
}

void stop_led_indicator() {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 255);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "LED indicator stopped.");
}