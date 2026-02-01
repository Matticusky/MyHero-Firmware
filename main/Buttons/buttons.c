#include <stdio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

#include "Buttons/buttons.h"

#define PLAY_PAUSE_BUTTON GPIO_NUM_18
#define ESP_RECORD_CTRL_BUTTON GPIO_NUM_10

// Timing thresholds in milliseconds
#define DEBOUNCE_MS             50      // Debounce time
#define DOUBLE_PRESS_WINDOW_MS  400     // Max time between presses for double press
#define LONG_PRESS_MS           2000    // Hold time for long press

static const char *TAG = "Buttons";

// Button state machine states
typedef enum {
    BTN_STATE_IDLE,             // Waiting for press
    BTN_STATE_PRESSED,          // Button is held down
    BTN_STATE_WAIT_SECOND,      // Released after short press, waiting for potential second press
    BTN_STATE_SECOND_PRESSED    // Second press detected, waiting for release
} button_state_t;

// Button context structure
typedef struct {
    gpio_num_t gpio;
    button_state_t state;
    uint64_t press_time;        // When button was pressed
    uint64_t release_time;      // When button was released
    uint64_t last_event_time;   // For debouncing
    volatile bool event_pending; // ISR signals an edge event
    void (*single_cb)(void);
    void (*double_cb)(void);
    void (*long_cb)(void);
    const char *name;
} button_ctx_t;

static button_ctx_t play_pause_btn = {
    .gpio = PLAY_PAUSE_BUTTON,
    .state = BTN_STATE_IDLE,
    .name = "PlayPause"
};

static button_ctx_t record_ctrl_btn = {
    .gpio = ESP_RECORD_CTRL_BUTTON,
    .state = BTN_STATE_IDLE,
    .name = "RecordCtrl"
};

// ISR handlers - just signal that an event occurred
void IRAM_ATTR play_pause_button_isr_handler(void *arg) {
    play_pause_btn.event_pending = true;
}

void IRAM_ATTR esp_record_ctrl_button_isr_handler(void *arg) {
    record_ctrl_btn.event_pending = true;
}

// Process button state machine
static void process_button(button_ctx_t *btn) {
    uint64_t now_ms = esp_timer_get_time() / 1000;
    bool is_pressed = (gpio_get_level(btn->gpio) == 0);

    // Handle event from ISR with debouncing
    if (btn->event_pending) {
        btn->event_pending = false;

        // Debounce check
        if ((now_ms - btn->last_event_time) < DEBOUNCE_MS) {
            return;
        }
        btn->last_event_time = now_ms;
    }

    switch (btn->state) {
        case BTN_STATE_IDLE:
            if (is_pressed) {
                btn->press_time = now_ms;
                btn->state = BTN_STATE_PRESSED;
                ESP_LOGD(TAG, "%s: IDLE -> PRESSED", btn->name);
            }
            break;

        case BTN_STATE_PRESSED:
            if (!is_pressed) {
                // Button released
                uint64_t hold_time = now_ms - btn->press_time;

                if (hold_time >= LONG_PRESS_MS) {
                    // Long press detected
                    ESP_LOGI(TAG, "%s: Long press detected", btn->name);
                    if (btn->long_cb) {
                        btn->long_cb();
                    }
                    btn->state = BTN_STATE_IDLE;
                } else {
                    // Short press - wait for potential second press
                    btn->release_time = now_ms;
                    btn->state = BTN_STATE_WAIT_SECOND;
                    ESP_LOGD(TAG, "%s: PRESSED -> WAIT_SECOND", btn->name);
                }
            } else {
                // Still pressed - check for long press
                uint64_t hold_time = now_ms - btn->press_time;
                if (hold_time >= LONG_PRESS_MS) {
                    // Long press detected while still holding
                    ESP_LOGI(TAG, "%s: Long press detected (held)", btn->name);
                    if (btn->long_cb) {
                        btn->long_cb();
                    }
                    // Wait for release before going back to idle
                    while (gpio_get_level(btn->gpio) == 0) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    btn->state = BTN_STATE_IDLE;
                    btn->last_event_time = esp_timer_get_time() / 1000;
                }
            }
            break;

        case BTN_STATE_WAIT_SECOND:
            if (is_pressed) {
                // Second press started
                btn->press_time = now_ms;
                btn->state = BTN_STATE_SECOND_PRESSED;
                ESP_LOGD(TAG, "%s: WAIT_SECOND -> SECOND_PRESSED", btn->name);
            } else {
                // Check if double press window expired
                if ((now_ms - btn->release_time) >= DOUBLE_PRESS_WINDOW_MS) {
                    // Single press confirmed
                    ESP_LOGI(TAG, "%s: Single press detected", btn->name);
                    if (btn->single_cb) {
                        btn->single_cb();
                    }
                    btn->state = BTN_STATE_IDLE;
                }
            }
            break;

        case BTN_STATE_SECOND_PRESSED:
            if (!is_pressed) {
                // Second press released - double press confirmed
                ESP_LOGI(TAG, "%s: Double press detected", btn->name);
                if (btn->double_cb) {
                    btn->double_cb();
                }
                btn->state = BTN_STATE_IDLE;
            }
            break;
    }
}

void button_scanning_task(void *pvParameters) {
    ESP_LOGI(TAG, "Button scanning task started");
    init_buttons();

    while (1) {
        process_button(&play_pause_btn);
        process_button(&record_ctrl_btn);
        vTaskDelay(pdMS_TO_TICKS(20)); // Poll every 20ms for responsive detection
    }
}

void init_buttons(void) {
    ESP_LOGI(TAG, "Initializing buttons...");

    // Configure Play/Pause button
    gpio_reset_pin(PLAY_PAUSE_BUTTON);
    gpio_set_direction(PLAY_PAUSE_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PLAY_PAUSE_BUTTON, GPIO_PULLUP_ONLY);

    // Configure Record Control button
    gpio_reset_pin(ESP_RECORD_CTRL_BUTTON);
    gpio_set_direction(ESP_RECORD_CTRL_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ESP_RECORD_CTRL_BUTTON, GPIO_PULLUP_ONLY);

    // Install ISR service and handlers
    ESP_LOGI(TAG, "Configuring button interrupts...");
    gpio_set_intr_type(PLAY_PAUSE_BUTTON, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(ESP_RECORD_CTRL_BUTTON, GPIO_INTR_ANYEDGE);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PLAY_PAUSE_BUTTON, play_pause_button_isr_handler, NULL);
    gpio_isr_handler_add(ESP_RECORD_CTRL_BUTTON, esp_record_ctrl_button_isr_handler, NULL);

    gpio_intr_enable(PLAY_PAUSE_BUTTON);
    gpio_intr_enable(ESP_RECORD_CTRL_BUTTON);

    ESP_LOGI(TAG, "Buttons initialized successfully");
}

void start_button_scanning_task(void) {
    ESP_LOGI(TAG, "Starting button scanning task...");
    xTaskCreatePinnedToCore(
        button_scanning_task,
        "button_scan",
        4096,
        NULL,
        20,
        NULL,
        1
    );
}

// Callback setters for Play/Pause button
void set_play_pause_button_single_press_callback(void (*callback)(void)) {
    play_pause_btn.single_cb = callback;
}

void set_play_pause_button_double_press_callback(void (*callback)(void)) {
    play_pause_btn.double_cb = callback;
}

void set_play_pause_button_long_press_callback(void (*callback)(void)) {
    play_pause_btn.long_cb = callback;
}

void clear_play_pause_button_single_press_callback(void) {
    play_pause_btn.single_cb = NULL;
}

void clear_play_pause_button_double_press_callback(void) {
    play_pause_btn.double_cb = NULL;
}

void clear_play_pause_button_long_press_callback(void) {
    play_pause_btn.long_cb = NULL;
}

// Callback setters for Record Control button
void set_esp_record_ctrl_button_single_press_callback(void (*callback)(void)) {
    record_ctrl_btn.single_cb = callback;
}

void set_esp_record_ctrl_button_double_press_callback(void (*callback)(void)) {
    record_ctrl_btn.double_cb = callback;
}

void set_esp_record_ctrl_button_long_press_callback(void (*callback)(void)) {
    record_ctrl_btn.long_cb = callback;
}

void clear_esp_record_ctrl_button_single_press_callback(void) {
    record_ctrl_btn.single_cb = NULL;
}

void clear_esp_record_ctrl_button_double_press_callback(void) {
    record_ctrl_btn.double_cb = NULL;
}

void clear_esp_record_ctrl_button_long_press_callback(void) {
    record_ctrl_btn.long_cb = NULL;
}
