#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

void init_audio_system();
void record_button_press_handler();
void play_pause_button_press_handler();

#ifdef __cplusplus
}
#endif
#endif // AUDIO_H