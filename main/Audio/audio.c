#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_err.h>
#include <audio_element.h>
#include <audio_pipeline.h>
#include <audio_event_iface.h>
#include <audio_mem.h>
#include <audio_common.h>
#include <i2s_stream.h>
#include <wav_encoder.h>
#include <wav_decoder.h>
#include <fatfs_stream.h>
#include <audio_alc.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#include "audio.h"
#include "../Storage/storage.h"
#include "../Volume/volume.h"
#include "../Playlist/playlist.h"
#include "../BLE/ble.h"
#include "../Indicator/indicator.h"

#define SPEAKER_ENABLE_PIN GPIO_NUM_34

static const char *TAG = "Audio";

// Audio state
static audio_state_t current_state = AUDIO_STATE_IDLE;
static SemaphoreHandle_t audio_mutex = NULL;
static TaskHandle_t playback_task_handle = NULL;
static TaskHandle_t recording_task_handle = NULL;

// Control flags
static volatile bool stop_playback_requested = false;
static volatile bool stop_recording_requested = false;

// Active ALC element for volume control during playback
static audio_element_handle_t active_alc_el = NULL;

// Last recording path
static char last_recording_path[128] = {0};

// ============ Helper Functions ============

static void enable_speaker(void) {
    gpio_set_level(SPEAKER_ENABLE_PIN, 1);
    ESP_LOGI(TAG, "Speaker enabled");
}

static void disable_speaker(void) {
    gpio_set_level(SPEAKER_ENABLE_PIN, 0);
    ESP_LOGI(TAG, "Speaker disabled");
}

// ============ State Functions ============

audio_state_t audio_get_state(void) {
    return current_state;
}

bool audio_is_playing(void) {
    return (current_state == AUDIO_STATE_PLAYING);
}

bool audio_is_recording(void) {
    return (current_state == AUDIO_STATE_RECORDING);
}

bool audio_is_paused(void) {
    return (current_state == AUDIO_STATE_PAUSED);
}

const char* audio_get_last_recording(void) {
    if (last_recording_path[0] == '\0') {
        return NULL;
    }
    return last_recording_path;
}

// ============ Initialization ============

void init_audio_system(void) {
    ESP_LOGI(TAG, "Initializing audio system...");

    // Initialize speaker enable GPIO
    gpio_reset_pin(SPEAKER_ENABLE_PIN);
    gpio_set_direction(SPEAKER_ENABLE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SPEAKER_ENABLE_PIN, 0);

    // Create mutex
    audio_mutex = xSemaphoreCreateMutex();
    if (audio_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create audio mutex");
        return;
    }

    current_state = AUDIO_STATE_IDLE;
    ESP_LOGI(TAG, "Audio system initialized");
}

// ============ Playback Implementation ============

typedef struct {
    char file_path[128];
    bool auto_advance;
} playback_params_t;

static void playback_task(void *pvParameters) {
    playback_params_t *params = (playback_params_t *)pvParameters;
    char file_path[128];
    bool auto_advance = params->auto_advance;
    bool should_advance = false;  // Track if we should auto-advance after cleanup
    strncpy(file_path, params->file_path, sizeof(file_path) - 1);
    free(params);

    // Take mutex
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire audio mutex");
        playback_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    current_state = AUDIO_STATE_PLAYING;
    stop_playback_requested = false;

    ESP_LOGI(TAG, "========================================");
    const char *filename = strrchr(file_path, '/');
    filename = filename ? filename + 1 : file_path;
    ESP_LOGI(TAG, "  NOW PLAYING: %s", filename);
    ESP_LOGI(TAG, "  Track %d of %d", playlist_get_current_index() + 1, playlist_get_count());
    ESP_LOGI(TAG, "========================================");

    // Create audio elements
    ESP_LOGI(TAG, "Creating audio pipeline...");

    // FATFS Reader
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t fatfs_reader = fatfs_stream_init(&fatfs_cfg);
    if (!fatfs_reader) {
        ESP_LOGE(TAG, "Failed to create FATFS reader");
        goto cleanup;
    }

    // WAV Decoder
    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    audio_element_handle_t wav_dec = wav_decoder_init(&wav_cfg);
    if (!wav_dec) {
        ESP_LOGE(TAG, "Failed to create WAV decoder");
        audio_element_deinit(fatfs_reader);
        goto cleanup;
    }

    // ALC Volume Control Element
    alc_volume_setup_cfg_t alc_cfg = DEFAULT_ALC_VOLUME_SETUP_CONFIG();
    audio_element_handle_t alc_el = alc_volume_setup_init(&alc_cfg);
    if (!alc_el) {
        ESP_LOGE(TAG, "Failed to create ALC element");
        audio_element_deinit(fatfs_reader);
        audio_element_deinit(wav_dec);
        goto cleanup;
    }

    // I2S Writer (without internal ALC - using separate ALC element)
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.use_alc = false;  // Disabled - using separate ALC element
    i2s_cfg.chan_cfg.id = I2S_NUM_1;
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    // Explicitly set I2S output GPIO pins
    i2s_cfg.std_cfg.gpio_cfg.bclk = GPIO_NUM_47;
    i2s_cfg.std_cfg.gpio_cfg.ws = GPIO_NUM_48;
    i2s_cfg.std_cfg.gpio_cfg.dout = GPIO_NUM_33;
    i2s_cfg.std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    i2s_cfg.std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);
    if (!i2s_writer) {
        ESP_LOGE(TAG, "Failed to create I2S writer");
        audio_element_deinit(fatfs_reader);
        audio_element_deinit(wav_dec);
        audio_element_deinit(alc_el);
        goto cleanup;
    }

    // Create pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline) {
        ESP_LOGE(TAG, "Failed to create pipeline");
        audio_element_deinit(fatfs_reader);
        audio_element_deinit(wav_dec);
        audio_element_deinit(alc_el);
        audio_element_deinit(i2s_writer);
        goto cleanup;
    }

    // Register and link elements: file → wav → alc → i2s
    audio_pipeline_register(pipeline, fatfs_reader, "file");
    audio_pipeline_register(pipeline, wav_dec, "wav");
    audio_pipeline_register(pipeline, alc_el, "alc");
    audio_pipeline_register(pipeline, i2s_writer, "i2s");

    const char *link_tag[] = {"file", "wav", "alc", "i2s"};
    audio_pipeline_link(pipeline, link_tag, 4);

    // Set file URI
    audio_element_set_uri(fatfs_reader, file_path);

    // Create event interface
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    // Start pipeline
    audio_pipeline_run(pipeline);

    // Enable speaker and set volume via ALC element
    enable_speaker();
    active_alc_el = alc_el;  // Store for live volume updates
    alc_volume_setup_set_channel(alc_el, 1);  // Mono
    alc_volume_setup_set_volume(alc_el, volume_get_raw_value());
    ESP_LOGI(TAG, "Volume: %d dB", volume_get_raw_value());

    // Set LED mode
    led_set_mode(LED_MODE_PLAYING);

    // Event loop
    bool track_finished = false;
    uint64_t start_time = esp_timer_get_time();

    while (!stop_playback_requested && !track_finished) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, pdMS_TO_TICKS(500));

        if (ret == ESP_OK) {
            // Handle music info
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *)wav_dec &&
                msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {

                audio_element_info_t info;
                audio_element_getinfo(wav_dec, &info);
                ESP_LOGI(TAG, "Music info: %d Hz, %d ch, %d bits",
                         info.sample_rates, info.channels, info.bits);
                i2s_stream_set_clk(i2s_writer, info.sample_rates, info.bits, info.channels);
                // Update ALC channel count based on actual audio
                alc_volume_setup_set_channel(alc_el, info.channels);
            }

            // Handle track end
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *)i2s_writer &&
                msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {

                int status = (int)msg.data;
                if (status == AEL_STATUS_STATE_FINISHED || status == AEL_STATUS_STATE_STOPPED) {
                    ESP_LOGI(TAG, "Track finished (status: %d)", status);
                    track_finished = true;
                }
            }
        }

        // Log progress every second (only if we didn't receive an event)
        if (ret != ESP_OK) {
            uint32_t elapsed = (uint32_t)((esp_timer_get_time() - start_time) / 1000000);
            ESP_LOGD(TAG, "[PLAY] %s - %02lu:%02lu", filename,
                     (unsigned long)(elapsed / 60), (unsigned long)(elapsed % 60));
        }
    }

    // Cleanup pipeline
    ESP_LOGI(TAG, "Stopping pipeline...");
    active_alc_el = NULL;  // Clear before cleanup
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_event_iface_destroy(evt);
    audio_pipeline_unregister(pipeline, fatfs_reader);
    audio_pipeline_unregister(pipeline, wav_dec);
    audio_pipeline_unregister(pipeline, alc_el);
    audio_pipeline_unregister(pipeline, i2s_writer);
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(fatfs_reader);
    audio_element_deinit(wav_dec);
    audio_element_deinit(alc_el);
    audio_element_deinit(i2s_writer);
    ESP_LOGI(TAG, "Pipeline cleaned up");

    disable_speaker();

    // Handle auto-advance
    should_advance = track_finished && auto_advance && !stop_playback_requested;

cleanup:
    current_state = AUDIO_STATE_IDLE;
    playback_task_handle = NULL;

    // Set LED back to idle (unless BLE advertising)
    if (!ble_is_advertising()) {
        led_set_mode(LED_MODE_IDLE);
    }

    xSemaphoreGive(audio_mutex);

    // Auto-advance to next track
    if (should_advance) {
        ESP_LOGI(TAG, "Auto-advancing to next track...");
        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay before next track
        const char *next = playlist_next();
        if (next != NULL) {
            audio_play_file(next);
        } else {
            ESP_LOGI(TAG, "End of playlist");
        }
    }

    vTaskDelete(NULL);
}

esp_err_t audio_play_file(const char *file_path) {
    if (file_path == NULL) {
        ESP_LOGE(TAG, "Invalid file path");
        return ESP_ERR_INVALID_ARG;
    }

    if (current_state == AUDIO_STATE_RECORDING) {
        ESP_LOGW(TAG, "Cannot play while recording");
        return ESP_ERR_INVALID_STATE;
    }

    // Stop any current playback
    if (playback_task_handle != NULL) {
        audio_stop_playback();
        // Wait for task to finish
        int wait_count = 0;
        while (playback_task_handle != NULL && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_count++;
        }
    }

    // Allocate params
    playback_params_t *params = malloc(sizeof(playback_params_t));
    if (params == NULL) {
        ESP_LOGE(TAG, "Failed to allocate playback params");
        return ESP_ERR_NO_MEM;
    }
    strncpy(params->file_path, file_path, sizeof(params->file_path) - 1);
    params->file_path[sizeof(params->file_path) - 1] = '\0';
    params->auto_advance = true;

    // Create playback task
    BaseType_t ret = xTaskCreate(playback_task, "playback", 8192, params, 15, &playback_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create playback task");
        free(params);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void audio_stop_playback(void) {
    if (playback_task_handle != NULL) {
        ESP_LOGI(TAG, "Requesting playback stop...");
        stop_playback_requested = true;
    }
}

void audio_update_volume(void) {
    if (active_alc_el != NULL && current_state == AUDIO_STATE_PLAYING) {
        alc_volume_setup_set_volume(active_alc_el, volume_get_raw_value());
        ESP_LOGI(TAG, "Volume updated: %d dB", volume_get_raw_value());
    }
}

// ============ Recording Implementation ============

static void recording_task(void *pvParameters) {
    // Take mutex
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire audio mutex for recording");
        recording_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    current_state = AUDIO_STATE_RECORDING;
    stop_recording_requested = false;

    // Generate recording path
    if (storage_generate_recording_path(last_recording_path, sizeof(last_recording_path)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate recording path");
        goto cleanup;
    }

    const char *filename = strrchr(last_recording_path, '/');
    filename = filename ? filename + 1 : last_recording_path;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  RECORDING: %s", filename);
    ESP_LOGI(TAG, "========================================");

    // Create audio elements
    ESP_LOGI(TAG, "Creating recording pipeline...");

    // I2S Reader (PDM microphone - MMICT390200012)
    // Recording at 16kHz for smaller file size
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_cfg.chan_cfg.id = I2S_NUM_0;
    i2s_cfg.transmit_mode = I2S_COMM_MODE_PDM;

    // PDM clock configuration
    i2s_cfg.pdm_rx_cfg.clk_cfg.sample_rate_hz = 16000;
    i2s_cfg.pdm_rx_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;

    // PDM slot configuration - mono, left channel (try left if right doesn't work)
    i2s_cfg.pdm_rx_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    i2s_cfg.pdm_rx_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    i2s_cfg.pdm_rx_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.pdm_rx_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_RIGHT

    // PDM GPIO pins (CLK=35, DIN=36)
    i2s_cfg.pdm_rx_cfg.gpio_cfg.clk = GPIO_NUM_35;
    i2s_cfg.pdm_rx_cfg.gpio_cfg.din = GPIO_NUM_36;

    audio_element_handle_t i2s_reader = i2s_stream_init(&i2s_cfg);
    if (!i2s_reader) {
        ESP_LOGE(TAG, "Failed to create I2S reader");
        goto cleanup;
    }

    // WAV Encoder (16kHz, 16-bit, mono)
    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    audio_element_handle_t wav_enc = wav_encoder_init(&wav_cfg);
    if (!wav_enc) {
        ESP_LOGE(TAG, "Failed to create WAV encoder");
        audio_element_deinit(i2s_reader);
        goto cleanup;
    }

    // FATFS Writer
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t fatfs_writer = fatfs_stream_init(&fatfs_cfg);
    if (!fatfs_writer) {
        ESP_LOGE(TAG, "Failed to create FATFS writer");
        audio_element_deinit(i2s_reader);
        audio_element_deinit(wav_enc);
        goto cleanup;
    }

    // Create pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline) {
        ESP_LOGE(TAG, "Failed to create recording pipeline");
        audio_element_deinit(i2s_reader);
        audio_element_deinit(wav_enc);
        audio_element_deinit(fatfs_writer);
        goto cleanup;
    }

    // Register and link elements
    audio_pipeline_register(pipeline, i2s_reader, "i2s");
    audio_pipeline_register(pipeline, wav_enc, "wav");
    audio_pipeline_register(pipeline, fatfs_writer, "file");

    const char *link_tag[] = {"i2s", "wav", "file"};
    audio_pipeline_link(pipeline, link_tag, 3);

    // Set output file
    audio_element_set_uri(fatfs_writer, last_recording_path);

    // Start pipeline
    audio_pipeline_run(pipeline);

    // Set LED mode
    led_set_mode(LED_MODE_RECORDING);

    ESP_LOGI(TAG, "Recording started");
    uint64_t start_time = esp_timer_get_time();

    // Recording loop
    while (!stop_recording_requested) {
        uint32_t elapsed = (uint32_t)((esp_timer_get_time() - start_time) / 1000000);
        ESP_LOGI(TAG, "[REC] %s - %02lu:%02lu", filename,
                 (unsigned long)(elapsed / 60), (unsigned long)(elapsed % 60));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Get final duration
    uint32_t total_sec = (uint32_t)((esp_timer_get_time() - start_time) / 1000000);

    // Stop and cleanup pipeline
    ESP_LOGI(TAG, "Stopping recording pipeline...");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_unregister(pipeline, i2s_reader);
    audio_pipeline_unregister(pipeline, wav_enc);
    audio_pipeline_unregister(pipeline, fatfs_writer);
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(i2s_reader);
    audio_element_deinit(wav_enc);
    audio_element_deinit(fatfs_writer);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  RECORDING STOPPED: %s", filename);
    ESP_LOGI(TAG, "  Duration: %02lu:%02lu", (unsigned long)(total_sec / 60), (unsigned long)(total_sec % 60));
    ESP_LOGI(TAG, "========================================");

cleanup:
    current_state = AUDIO_STATE_IDLE;
    recording_task_handle = NULL;

    // Set LED back to idle (unless BLE advertising)
    if (!ble_is_advertising()) {
        led_set_mode(LED_MODE_IDLE);
    }

    xSemaphoreGive(audio_mutex);
    vTaskDelete(NULL);
}

esp_err_t audio_start_recording(void) {
    if (current_state != AUDIO_STATE_IDLE) {
        ESP_LOGW(TAG, "Cannot record in current state: %d", current_state);
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreate(recording_task, "recording", 8192, NULL, 15, &recording_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recording task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void audio_stop_recording(void) {
    if (recording_task_handle != NULL) {
        ESP_LOGI(TAG, "Requesting recording stop...");
        stop_recording_requested = true;
    }
}

// ============ Button Handlers ============

void play_pause_single_handler(void) {
    ESP_LOGI(TAG, "Play/Pause button, state: %d", current_state);

    switch (current_state) {
        case AUDIO_STATE_IDLE: {
            const char *track = playlist_get_current();
            if (track != NULL) {
                audio_play_file(track);
            } else {
                ESP_LOGW(TAG, "Playlist is empty");
            }
            break;
        }

        case AUDIO_STATE_PLAYING:
            // For now, stop playback (pause not implemented with new architecture)
            audio_stop_playback();
            ESP_LOGI(TAG, "Playback stopped");
            break;

        case AUDIO_STATE_PAUSED:
            // Resume - restart from current track
            {
                const char *track = playlist_get_current();
                if (track != NULL) {
                    audio_play_file(track);
                }
            }
            break;

        case AUDIO_STATE_RECORDING:
            ESP_LOGW(TAG, "Ignoring play/pause during recording");
            break;
    }
}

void play_pause_double_handler(void) {
    ESP_LOGI(TAG, "Next track, state: %d", current_state);

    if (current_state == AUDIO_STATE_PLAYING) {
        // Stop current, then advance and play
        audio_stop_playback();
        // Wait for stop
        int wait = 0;
        while (playback_task_handle != NULL && wait < 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait++;
        }
    }

    if (current_state == AUDIO_STATE_IDLE || current_state == AUDIO_STATE_PAUSED) {
        const char *next = playlist_next();
        if (next != NULL) {
            ESP_LOGI(TAG, "Playing next track: %s", next);
            audio_play_file(next);
        } else {
            ESP_LOGW(TAG, "No next track");
        }
    }
}

void record_single_handler(void) {
    ESP_LOGI(TAG, "Record single press, state: %d", current_state);

    if (current_state == AUDIO_STATE_RECORDING) {
        audio_stop_recording();
        // Wait for recording to stop
        int wait = 0;
        while (recording_task_handle != NULL && wait < 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait++;
        }
        // Rescan playlist
        playlist_rescan();
    } else {
        // Cycle volume
        volume_level_t new_level = volume_cycle();
        ESP_LOGI(TAG, "Volume: level %d (%d dB)", new_level, volume_get_raw_value());
        // Apply volume change immediately if playing
        audio_update_volume();
    }
}

void record_double_handler(void) {
    ESP_LOGI(TAG, "Record double press, state: %d", current_state);

    if (current_state == AUDIO_STATE_RECORDING) {
        audio_stop_recording();
        // Wait for stop
        int wait = 0;
        while (recording_task_handle != NULL && wait < 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait++;
        }
        playlist_rescan();
    } else if (current_state == AUDIO_STATE_PLAYING) {
        // Stop playback first
        audio_stop_playback();
        int wait = 0;
        while (playback_task_handle != NULL && wait < 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait++;
        }
        // Start recording
        audio_start_recording();
    } else if (current_state == AUDIO_STATE_IDLE || current_state == AUDIO_STATE_PAUSED) {
        audio_start_recording();
    }
}

// Legacy handlers
void record_button_press_handler(void) {
    record_double_handler();
}

void play_pause_button_press_handler(void) {
    play_pause_single_handler();
}
