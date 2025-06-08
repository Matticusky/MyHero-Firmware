#ifndef BUTTONS_H
#define BUTTONS_H
#ifdef __cplusplus
extern "C" {
#endif

void init_buttons();

void set_play_pause_button_long_press_callback(void (*callback)(void));
void set_play_pause_button_double_press_callback(void (*callback)(void));
void set_play_pause_button_single_press_callback(void (*callback)(void));

void clear_play_pause_button_long_press_callback();
void clear_play_pause_button_double_press_callback();
void clear_play_pause_button_single_press_callback();

void set_esp_record_ctrl_button_long_press_callback(void (*callback)(void));
void set_esp_record_ctrl_button_double_press_callback(void (*callback)(void));
void set_esp_record_ctrl_button_single_press_callback(void (*callback)(void));

void clear_esp_record_ctrl_button_long_press_callback();
void clear_esp_record_ctrl_button_double_press_callback();
void clear_esp_record_ctrl_button_single_press_callback();

void start_button_scanning_task();

#ifdef __cplusplus
}
#endif
#endif // BUTTONS_H