#include "indicator.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define LED_INDICATOR_PIN GPIO_NUM_21

static const char *TAG = "Indicator";

// Current LED mode
static led_mode_t current_mode = LED_MODE_OFF;
static TaskHandle_t led_task_handle = NULL;
static volatile bool task_running = false;

// Pattern timing constants (in milliseconds)
#define RECORDING_ON_MS     100     // 0.1s on
#define RECORDING_OFF_MS    900     // 0.9s off

#define PLAYING_ON_MS       500     // 0.5s on
#define PLAYING_OFF_MS      4500    // 4.5s off (total 5s period)

#define TRANSFER_ON_MS      100     // 0.1s on (fast pulse for file transfer)
#define TRANSFER_OFF_MS     100     // 0.1s off

// Low-level LED control (active low: GPIO LOW = LED ON)
static void led_gpio_on(void) {
    gpio_set_level(LED_INDICATOR_PIN, 0);  // Active low: 0 = ON
}

static void led_gpio_off(void) {
    gpio_set_level(LED_INDICATOR_PIN, 1);  // Active low: 1 = OFF
}

// LED pattern task
static void led_pattern_task(void *pvParameters) {
    task_running = true;
    ESP_LOGI(TAG, "LED pattern task started");

    while (task_running) {
        switch (current_mode) {
            case LED_MODE_OFF:
            case LED_MODE_IDLE:
                led_gpio_off();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_MODE_ON:
            case LED_MODE_BLE_PAIRING:
                led_gpio_on();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_MODE_RECORDING:
                // Blink: 0.1s on, 0.9s off
                led_gpio_on();
                vTaskDelay(pdMS_TO_TICKS(RECORDING_ON_MS));
                if (current_mode != LED_MODE_RECORDING) break;
                led_gpio_off();
                vTaskDelay(pdMS_TO_TICKS(RECORDING_OFF_MS));
                break;

            case LED_MODE_PLAYING:
                // Pulse: 0.5s on every 5s
                led_gpio_on();
                vTaskDelay(pdMS_TO_TICKS(PLAYING_ON_MS));
                if (current_mode != LED_MODE_PLAYING) break;
                led_gpio_off();
                vTaskDelay(pdMS_TO_TICKS(PLAYING_OFF_MS));
                break;

            case LED_MODE_BLE_TRANSFER:
                // Fast pulse: 0.1s on, 0.1s off (file transfer activity)
                led_gpio_on();
                vTaskDelay(pdMS_TO_TICKS(TRANSFER_ON_MS));
                if (current_mode != LED_MODE_BLE_TRANSFER) break;
                led_gpio_off();
                vTaskDelay(pdMS_TO_TICKS(TRANSFER_OFF_MS));
                break;

            default:
                led_gpio_off();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }

    led_gpio_off();
    ESP_LOGI(TAG, "LED pattern task stopped");
    vTaskDelete(NULL);
}

void init_led_indicator(void) {
    ESP_LOGI(TAG, "Initializing LED indicator...");

    // Configure GPIO for LED (active low)
    gpio_reset_pin(LED_INDICATOR_PIN);
    gpio_set_direction(LED_INDICATOR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_INDICATOR_PIN, 1);  // Start with LED off (active low: 1 = OFF)

    // Create LED pattern task
    if (led_task_handle == NULL) {
        xTaskCreate(
            led_pattern_task,
            "led_pattern_task",
            2048,
            NULL,
            5,  // Low priority
            &led_task_handle
        );
    }

    current_mode = LED_MODE_IDLE;
    ESP_LOGI(TAG, "LED indicator initialized");
}

void led_set_mode(led_mode_t mode) {
    if (mode == current_mode) {
        return;  // No change needed
    }

    ESP_LOGI(TAG, "LED mode changed: %d -> %d", current_mode, mode);
    current_mode = mode;
}

led_mode_t led_get_mode(void) {
    return current_mode;
}

void led_on(void) {
    led_set_mode(LED_MODE_ON);
}

void led_off(void) {
    led_set_mode(LED_MODE_OFF);
}

// Legacy functions for backward compatibility
static uint32_t legacy_frequency = 1000;
static uint8_t legacy_duty_cycle = 0;

void set_led_indicator_frequency(uint32_t frequency) {
    legacy_frequency = frequency;
    ESP_LOGI(TAG, "LED indicator frequency set to %lu Hz (legacy)", (unsigned long)legacy_frequency);
}

void set_led_indicator_duty(uint8_t duty) {
    legacy_duty_cycle = duty;
    ESP_LOGI(TAG, "LED indicator duty cycle set to %d (legacy)", legacy_duty_cycle);
}

void start_led_indicator(void) {
    // Legacy: use duty cycle to determine on/off
    if (legacy_duty_cycle > 0) {
        led_set_mode(LED_MODE_ON);
    } else {
        led_set_mode(LED_MODE_OFF);
    }
    ESP_LOGI(TAG, "LED indicator started (legacy)");
}

void stop_led_indicator(void) {
    led_set_mode(LED_MODE_OFF);
    ESP_LOGI(TAG, "LED indicator stopped (legacy)");
}
