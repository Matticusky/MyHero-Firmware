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

#define double_press_time_threshold 500000 // 500ms
#define long_press_time_threshold 2000000 // 2s

static const char *TAG = "Buttons : ";

volatile uint64_t play_pause_button_last_press_time = 0;
volatile uint64_t play_pause_button_press_start_time = 0;
volatile uint64_t record_ctrl_button_last_press_time = 0;
volatile uint64_t record_ctrl_button_press_start_time = 0;

void(* PLAY_PAUSE_BUTTON_single_press_callback)() = NULL;
void(* ESP_RECORD_CTRL_BUTTON_single_press_callback)() = NULL;
void(* PLAY_PAUSE_BUTTON_double_press_callback)() = NULL;
void(* ESP_RECORD_CTRL_BUTTON_double_press_callback)() = NULL;
void(* PLAY_PAUSE_BUTTON_long_press_callback)() = NULL;
void(* ESP_RECORD_CTRL_BUTTON_long_press_callback)() = NULL;

volatile bool play_pause_button_single_press_detected = false;
volatile bool esp_record_ctrl_button_single_press_detected = false;
volatile bool play_pause_button_double_press_detected = false;
volatile bool esp_record_ctrl_button_double_press_detected = false;
volatile bool play_pause_button_long_press_detected = false;
volatile bool esp_record_ctrl_button_long_press_detected = false;


void IRAM_ATTR play_pause_button_isr_handler(void *arg) {
    uint64_t current_time = esp_timer_get_time();
    if (gpio_get_level(PLAY_PAUSE_BUTTON) == 0) { // Button pressed
        play_pause_button_press_start_time = current_time; // Reset press start time
    }
    else{ //button released
        if((current_time - play_pause_button_press_start_time) > long_press_time_threshold){
            play_pause_button_long_press_detected = true;
        }
        else if((current_time - play_pause_button_last_press_time) < double_press_time_threshold){
            play_pause_button_double_press_detected = true;
            // Reset the last press time to avoid confusion with single press
            play_pause_button_last_press_time = 0; // Reset last press time to avoid confusion with single press
        }
        else{
            play_pause_button_last_press_time = current_time;
            play_pause_button_single_press_detected = true;
        }
    }
}

void IRAM_ATTR esp_record_ctrl_button_isr_handler(void *arg) {
    uint64_t current_time = esp_timer_get_time();
    if (gpio_get_level(ESP_RECORD_CTRL_BUTTON) == 0) { // Button pressed
        record_ctrl_button_press_start_time = current_time; // Reset press start time
    }
    else{ //button released
        if((current_time - record_ctrl_button_press_start_time) > long_press_time_threshold){
            esp_record_ctrl_button_long_press_detected = true;
        }
        else if((current_time - record_ctrl_button_last_press_time) < double_press_time_threshold){
            esp_record_ctrl_button_double_press_detected = true;
            // Reset the last press time to avoid confusion with single press
            record_ctrl_button_last_press_time = 0; // Reset last press time to avoid confusion with single press
        }
        else{
            record_ctrl_button_last_press_time = current_time;
            esp_record_ctrl_button_single_press_detected = true;
        }
    }
}


void button_scanning_task(){
    ESP_LOGI(TAG, "Button scanning task started...");
    init_buttons();
    while(1){
        if(play_pause_button_long_press_detected){
            play_pause_button_long_press_detected = false;
            if(PLAY_PAUSE_BUTTON_long_press_callback != NULL){
                PLAY_PAUSE_BUTTON_long_press_callback();
            }else{
                ESP_LOGW(TAG, "No callback set for PLAY_PAUSE_BUTTON long press.");
            }
        }
        if(play_pause_button_double_press_detected){
            play_pause_button_double_press_detected = false;
            if(PLAY_PAUSE_BUTTON_double_press_callback != NULL){
                PLAY_PAUSE_BUTTON_double_press_callback();
            }else{
                ESP_LOGW(TAG, "No callback set for PLAY_PAUSE_BUTTON double press.");
            }
        }
        if(play_pause_button_single_press_detected){
            // wait for timeout to ensure it's a single press
            vTaskDelay(pdMS_TO_TICKS(double_press_time_threshold/1000));
            play_pause_button_single_press_detected = false;
            if(!play_pause_button_double_press_detected){
                if(PLAY_PAUSE_BUTTON_single_press_callback != NULL){
                    PLAY_PAUSE_BUTTON_single_press_callback();
                }else{
                    ESP_LOGW(TAG, "No callback set for PLAY_PAUSE_BUTTON single press.");
                }
            }
        }


        if(esp_record_ctrl_button_long_press_detected){
            esp_record_ctrl_button_long_press_detected = false;
            if(ESP_RECORD_CTRL_BUTTON_long_press_callback != NULL){
                ESP_RECORD_CTRL_BUTTON_long_press_callback();
            }else{
                ESP_LOGW(TAG, "No callback set for ESP_RECORD_CTRL_BUTTON long press.");
            }
        }
        if(esp_record_ctrl_button_double_press_detected){
            esp_record_ctrl_button_double_press_detected = false;
            if(ESP_RECORD_CTRL_BUTTON_double_press_callback != NULL){
                ESP_RECORD_CTRL_BUTTON_double_press_callback();
            }else{
                ESP_LOGW(TAG, "No callback set for ESP_RECORD_CTRL_BUTTON double press.");
            }
        }
        if(esp_record_ctrl_button_single_press_detected){
            // wait for timeout to ensure it's a single press
            vTaskDelay(pdMS_TO_TICKS(double_press_time_threshold/1000));
            esp_record_ctrl_button_single_press_detected = false;
            if(!esp_record_ctrl_button_double_press_detected){
                if(ESP_RECORD_CTRL_BUTTON_single_press_callback != NULL){
                    ESP_RECORD_CTRL_BUTTON_single_press_callback();
                }else{
                    ESP_LOGW(TAG, "No callback set for ESP_RECORD_CTRL_BUTTON single press.");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay to avoid busy-waiting
    }
}



void init_buttons(){
    ESP_LOGI(TAG, "Initializing buttons...");
    gpio_reset_pin(PLAY_PAUSE_BUTTON);
    gpio_set_direction(PLAY_PAUSE_BUTTON, GPIO_MODE_INPUT);
    gpio_reset_pin(ESP_RECORD_CTRL_BUTTON);
    gpio_set_direction(ESP_RECORD_CTRL_BUTTON, GPIO_MODE_INPUT);

    // enable interrupts in level change
    ESP_LOGI(TAG, "Configuring button interrupts...");
    gpio_set_intr_type(PLAY_PAUSE_BUTTON, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(ESP_RECORD_CTRL_BUTTON, GPIO_INTR_ANYEDGE);
    // install gpio interrupt handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PLAY_PAUSE_BUTTON, play_pause_button_isr_handler, NULL);
    gpio_isr_handler_add(ESP_RECORD_CTRL_BUTTON, esp_record_ctrl_button_isr_handler, NULL);
    // enable interrupts
    gpio_intr_enable(PLAY_PAUSE_BUTTON);
    gpio_intr_enable(ESP_RECORD_CTRL_BUTTON);
    ESP_LOGI(TAG, "Buttons initialized successfully.");
}

void start_button_scanning_task(){
    ESP_LOGI(TAG, "Starting button scanning task...");
    xTaskCreatePinnedToCore(
        button_scanning_task,       // Task function
        "button_scanning_task",     // Name of the task
        4096,                       // Stack size in bytes
        NULL,                       // Task input parameter
        20,                          // Priority of the task
        NULL,                       // Task handle
        1              // Core where the task will run (no affinity)
    );
}

void set_play_pause_button_long_press_callback(void (*callback)(void)) {
    PLAY_PAUSE_BUTTON_long_press_callback = callback;
}
void set_play_pause_button_double_press_callback(void (*callback)(void)) {
    PLAY_PAUSE_BUTTON_double_press_callback = callback;
}
void set_play_pause_button_single_press_callback(void (*callback)(void)) {
    PLAY_PAUSE_BUTTON_single_press_callback = callback;
}
void clear_play_pause_button_long_press_callback() {
    PLAY_PAUSE_BUTTON_long_press_callback = NULL;
}
void clear_play_pause_button_double_press_callback() {
    PLAY_PAUSE_BUTTON_double_press_callback = NULL;
}
void clear_play_pause_button_single_press_callback() {
    PLAY_PAUSE_BUTTON_single_press_callback = NULL;
}

void set_esp_record_ctrl_button_long_press_callback(void (*callback)(void)) {
    ESP_RECORD_CTRL_BUTTON_long_press_callback = callback;
}
void set_esp_record_ctrl_button_double_press_callback(void (*callback)(void)) {
    ESP_RECORD_CTRL_BUTTON_double_press_callback = callback;
}
void set_esp_record_ctrl_button_single_press_callback(void (*callback)(void)) {
    ESP_RECORD_CTRL_BUTTON_single_press_callback = callback;
}
void clear_esp_record_ctrl_button_long_press_callback() {
    ESP_RECORD_CTRL_BUTTON_long_press_callback = NULL;
}
void clear_esp_record_ctrl_button_double_press_callback() {
    ESP_RECORD_CTRL_BUTTON_double_press_callback = NULL;
}
void clear_esp_record_ctrl_button_single_press_callback() {
    ESP_RECORD_CTRL_BUTTON_single_press_callback = NULL;
}