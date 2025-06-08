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
#include <audio_sys.h>
#include <audio_forge.h>
#include <i2s_stream.h>
#include <aac_encoder.h>
#include <aac_decoder.h>
#include <fatfs_stream.h>
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#include <Audio/audio.h>
#include <Storage/storage.h>

#define speaker_enable_pin GPIO_NUM_34

static const char *TAG = "Audio System : ";
// create a audio semaphore to synchronize audio operations
SemaphoreHandle_t audio_semaphore = NULL;
// audio elements for recorder
audio_element_handle_t *i2s_stream_reader, *aac_encoder, *fatfs_stream_writer;
audio_pipeline_handle_t *audio_pipeline_recorder;
bool is_recording = false; // Flag to indicate if recording is in progress
bool should_record = false; // Flag to indicate if recording should start

// audio elements for player
audio_element_handle_t *i2s_stream_writer, *aac_decoder, *audio_alc, *fatfs_stream_reader;
audio_pipeline_handle_t *audio_pipeline_player;
bool is_playing = false; // Flag to indicate if playback is in progress
bool should_play = false; // Flag to indicate if playback should start

// position markers for playlist
static int playlist_position = 0; // Current position in the playlist
static int playlist_size = 0; // Total number of files in the playlist

void disable_speaker(){
    ESP_LOGI(TAG, "Disabling speaker...");
    gpio_set_level(speaker_enable_pin, 0); // Disable speaker
}

void enable_speaker(){
    ESP_LOGI(TAG, "Enabling speaker...");
    gpio_set_level(speaker_enable_pin, 1); // Enable speaker
}

void init_audio_system(){
    ESP_LOGI(TAG, "Initializing audio system...");
    // Initialize gpio
    gpio_reset_pin(speaker_enable_pin);
    gpio_set_direction(speaker_enable_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(speaker_enable_pin, 0); // Disable speaker by default

    // initialize audio semaphore
    audio_semaphore = xSemaphoreCreateMutex();
    xSemaphoreGive(audio_semaphore); // Initialize semaphore to be available
    ESP_LOGI(TAG, "Audio semaphore created successfully.");
}

audio_element_handle_t setup_i2s_strem_reader(){
    ESP_LOGI(TAG, "Setting up I2S stream reader...");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_cfg.use_alc = true; // Enable Automatic Level Control (ALC)
    i2s_cfg.chan_cfg.id = I2S_NUM_0; // Use I2S_NUM_0 for reading
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 44100; // Set sample rate to 44.1 kHz
    i2s_cfg.transmit_mode = I2S_COMM_MODE_PDM; // Set to PDM mode for microphone input
    i2s_cfg.pdm_rx_cfg.clk_cfg.sample_rate_hz = 44100; // Set PDM sample rate to 44.1 kHz
    i2s_cfg.pdm_rx_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT; // Use default clock source
    i2s_cfg.pdm_rx_cfg.clk_cfg.mclk_multiple = 256; // Set MCLK multiple to 256 for PDM
    i2s_cfg.pdm_rx_cfg.clk_cfg.dn_sample_mode = I2S_PDM_DSR_8S; // Set down-sampling mode to 8x
    i2s_cfg.pdm_rx_cfg.clk_cfg.bclk_div = 8; // Set BCLK divider to 8 for PDM
    i2s_cfg.pdm_rx_cfg.slot_cfg.data_bit_width = 16; // Set data bit width to 16 bits for PDM
    i2s_cfg.pdm_rx_cfg.slot_cfg.slot_bit_width = 16; // Set slot bit width to 16 bits for PDM
    i2s_cfg.pdm_rx_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO; // Set slot mode to mono for PDM
    i2s_cfg.pdm_rx_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_RIGHT; // Set number of slots to 1 for PDM

    audio_element_handle_t *i2s_stream_reader = malloc(sizeof(audio_element_handle_t));
    *i2s_stream_reader = i2s_stream_init(&i2s_cfg);
    if(!*i2s_stream_reader) {
        ESP_LOGE(TAG, "Failed to create I2S stream reader.");
        free(i2s_stream_reader);
        return NULL;
    }
    ESP_LOGI(TAG, "I2S stream reader created successfully.");
    return i2s_stream_reader;
}

audio_element_handle_t setup_aac_encoder(){
    ESP_LOGI(TAG, "Setting up AAC encoder...");
    aac_encoder_cfg_t aac_cfg = DEFAULT_AAC_ENCODER_CONFIG();
    aac_cfg.sample_rate = 44100; // Set sample rate to 44.1 kHz
    aac_cfg.channel = 1; // Set channel count to 1 for mono
    aac_cfg.bitrate = 64000; // Set bit rate to 64 kbps
    aac_cfg.task_core = 0;

    audio_element_handle_t *aac_encoder = malloc(sizeof(audio_element_handle_t));
    *aac_encoder = aac_encoder_init(&aac_cfg);
    if(!*aac_encoder) {
        ESP_LOGE(TAG, "Failed to create AAC encoder.");
        free(aac_encoder);
        return NULL;
    }
    ESP_LOGI(TAG, "AAC encoder created successfully.");
    return aac_encoder;
}

audio_element_handle_t setup_fatfs_stream_writer(){
    ESP_LOGI(TAG, "Setting up FATFS stream writer...");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t *fatfs_stream_writer = malloc(sizeof(audio_element_handle_t));
    *fatfs_stream_writer = fatfs_stream_init(&fatfs_cfg);
    if(!*fatfs_stream_writer) {
        ESP_LOGE(TAG, "Failed to create FATFS stream writer.");
        free(fatfs_stream_writer);
        return NULL;
    }
    ESP_LOGI(TAG, "FATFS stream writer created successfully.");
    return fatfs_stream_writer;
}

audio_pipeline_handle_t *setup_audio_recording_pipeline( audio_element_handle_t *i2s_reader,
                                                       audio_element_handle_t *aac_encoder,
                                                       audio_element_handle_t *fatfs_writer){

    ESP_LOGI(TAG, "Setting up audio recording pipeline...");
    if(!i2s_reader || !aac_encoder || !fatfs_writer) {
        ESP_LOGE(TAG, "Invalid audio elements provided for recording pipeline.");
        return NULL;
    }
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t *pipeline = malloc(sizeof(audio_pipeline_handle_t));
    if(!pipeline) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio pipeline.");
        return NULL;
    }
    *pipeline = audio_pipeline_init(&pipeline_cfg);
    if(!*pipeline) {
        ESP_LOGE(TAG, "Failed to create audio pipeline.");
        free(pipeline);
        return NULL;
    }
    ESP_LOGI(TAG, "Audio pipeline created successfully.");
    // Register audio elements in the pipeline
    audio_pipeline_register(*pipeline, *i2s_reader, "i2s_reader");
    audio_pipeline_register(*pipeline, *aac_encoder, "aac_encoder");
    audio_pipeline_register(*pipeline, *fatfs_writer, "fatfs_writer");
    ESP_LOGI(TAG, "Audio elements registered in pipeline successfully.");
    // Link the audio elements in the pipeline
    const char *link_tag[3] = {"i2s_reader", "aac_encoder", "fatfs_writer"};
    audio_pipeline_link(*pipeline, link_tag, 3);
    return pipeline;
}
void cleanup_audio_recording(){
    if(audio_pipeline_recorder==NULL){
        return;
    }
    if (!is_recording) {
        ESP_LOGW(TAG, "Recording system is not initialized.");
        return;
    }
    ESP_LOGI(TAG, "Stopping audio recording...");
    xSemaphoreTake(audio_semaphore, portMAX_DELAY); // Wait for semaphore
    // Stop the audio pipeline
    audio_pipeline_stop(*audio_pipeline_recorder);
    audio_pipeline_wait_for_stop(*audio_pipeline_recorder);
    ESP_LOGI(TAG, "Audio recording stopped successfully.");
    // Unregister and deinitialize audio elements
    audio_pipeline_unregister(*audio_pipeline_recorder, *i2s_stream_reader);
    audio_pipeline_unregister(*audio_pipeline_recorder, *aac_encoder);
    audio_pipeline_unregister(*audio_pipeline_recorder, *fatfs_stream_writer);

    audio_element_deinit(*i2s_stream_reader);
    audio_element_deinit(*aac_encoder);
    audio_element_deinit(*fatfs_stream_writer);
    free(i2s_stream_reader);
    free(aac_encoder);
    free(fatfs_stream_writer);
    // Deinitialize the audio pipeline
    audio_pipeline_deinit(*audio_pipeline_recorder);
    free(audio_pipeline_recorder);
    audio_pipeline_recorder = NULL;
    i2s_stream_reader = NULL;
    aac_encoder = NULL;
    fatfs_stream_writer = NULL;
    is_recording = false; // Reset recording flag
    ESP_LOGI(TAG, "Audio recording system stopped and cleaned up successfully.");
    xSemaphoreGive(audio_semaphore); // Release semaphore
}

void start_recording_system(void *pvParameters){
    if (is_recording) {
        ESP_LOGW(TAG, "Recording system is already initialized.");
        return;
    }
    is_recording = true;
    ESP_LOGI(TAG, "Initializing recording system...");
    xSemaphoreTake(audio_semaphore, portMAX_DELAY); // Wait for semaphore
    // setup audio elements
    i2s_stream_reader = setup_i2s_strem_reader();
    aac_encoder = setup_aac_encoder();
    fatfs_stream_writer = setup_fatfs_stream_writer();
    if (!i2s_stream_reader || !aac_encoder || !fatfs_stream_writer) {
        ESP_LOGE(TAG, "Failed to initialize audio elements for recording.");
        cleanup_audio_recording();
        xSemaphoreGive(audio_semaphore); // Release semaphore
        return;
    }
    // setup audio pipeline
    audio_pipeline_recorder = setup_audio_recording_pipeline(i2s_stream_reader, aac_encoder, fatfs_stream_writer);
    if (!audio_pipeline_recorder) {
        ESP_LOGE(TAG, "Failed to create audio pipeline for recording.");
        cleanup_audio_recording();
        xSemaphoreGive(audio_semaphore); // Release semaphore
        return;
    }
    audio_element_set_codec_fmt(*aac_encoder, ESP_CODEC_TYPE_AAC);
    audio_element_info_t recording_info = {0};
    audio_element_getinfo(*i2s_stream_reader, &recording_info);
    audio_element_setinfo(*aac_encoder, &recording_info);
    audio_element_setinfo(*fatfs_stream_writer, &recording_info);
    // Set the output file path for FATFS stream writer
    char base_dir[20];
    get_base_path(base_dir, 20);
    char output_file[50];
    snprintf(output_file, sizeof(output_file), "%s/recording.aac", base_dir);
    ESP_LOGI(TAG, "Output file path: %s", output_file);
    audio_element_set_uri(*fatfs_stream_writer, output_file);
    // Start the audio pipeline
    audio_pipeline_run(*audio_pipeline_recorder);
    i2s_alc_volume_set(*i2s_stream_reader, 30); // Set volume to 100%
    is_recording = true; // Set recording flag
    should_record = true; // Set flag to indicate recording should start
    uint64_t tick_start = esp_timer_get_time();
    ESP_LOGI(TAG, "Audio recording system initialized successfully.");
    while (should_record)
    {
        ESP_LOGI(TAG, "Recorded audio for %lld seconds", (esp_timer_get_time() - tick_start) / 1000000);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
    }
    xSemaphoreGive(audio_semaphore); // Release semaphore
    ESP_LOGI(TAG, "Audio recording system started successfully.");
    cleanup_audio_recording(); // Stop the recording system after initialization
    is_recording = false; // Reset recording flag
    vTaskDelete(NULL); // Delete the task after stopping
}


void start_audio_recording_task(){
    xTaskCreate(start_recording_system, "start_recording_system", 8096, NULL, 15, NULL);
}

void record_button_press_handler(){
    if (is_recording) {
        ESP_LOGI(TAG, "Stopping audio recording...");
        should_record = false; // Set flag to stop recording
    } else {
        ESP_LOGI(TAG, "Starting audio recording...");
        start_audio_recording_task(); // Start the recording task
    }
}

audio_element_handle_t *setup_fatfs_stream_reader(){
    ESP_LOGI(TAG, "Setting up FATFS stream reader...");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t *fatfs_stream_reader = malloc(sizeof(audio_element_handle_t));
    *fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
    if(!*fatfs_stream_reader) {
        ESP_LOGE(TAG, "Failed to create FATFS stream reader.");
        free(fatfs_stream_reader);
        return NULL;
    }
    ESP_LOGI(TAG, "FATFS stream reader created successfully.");
    return fatfs_stream_reader;
}

audio_element_handle_t *setup_aac_decoder(){
    ESP_LOGI(TAG, "Setting up AAC decoder...");
    aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
    audio_element_handle_t *aac_decoder = malloc(sizeof(audio_element_handle_t));
    *aac_decoder = aac_decoder_init(&aac_cfg);
    if(!*aac_decoder) {
        ESP_LOGE(TAG, "Failed to create AAC decoder.");
        free(aac_decoder);
        return NULL;
    }
    ESP_LOGI(TAG, "AAC decoder created successfully.");
    return aac_decoder;
}

audio_element_handle_t *setup_i2s_stream_writer(){
    ESP_LOGI(TAG, "Setting up I2S stream writer...");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.use_alc = true; // Enable Automatic Level Control (ALC)
    i2s_cfg.chan_cfg.id = I2S_NUM_1; // Use I2S_NUM_1 for playback
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO; // Set slot mode to mono
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT; // Set number of slots to 1
    audio_element_handle_t *i2s_stream_writer = malloc(sizeof(audio_element_handle_t));
    *i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if(!*i2s_stream_writer) {
        ESP_LOGE(TAG, "Failed to create I2S stream writer.");
        free(i2s_stream_writer);
        return NULL;
    }
    ESP_LOGI(TAG, "I2S stream writer created successfully.");
    return i2s_stream_writer;
}

audio_pipeline_handle_t *setup_audio_playback_pipeline(audio_element_handle_t *i2s_writer,
                                                        audio_element_handle_t *aac_decoder,
                                                        audio_element_handle_t *fatfs_reader){

    ESP_LOGI(TAG, "Setting up audio playback pipeline...");
    if(!i2s_writer || !aac_decoder || !fatfs_reader) {
        ESP_LOGE(TAG, "Invalid audio elements provided for playback pipeline.");
        return NULL;
    }
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t *pipeline = malloc(sizeof(audio_pipeline_handle_t));
    if(!pipeline) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio pipeline.");
        return NULL;
    }
    *pipeline = audio_pipeline_init(&pipeline_cfg);
    if(!*pipeline) {
        ESP_LOGE(TAG, "Failed to create audio pipeline.");
        free(pipeline);
        return NULL;
    }
    ESP_LOGI(TAG, "Audio pipeline created successfully.");
    // Register audio elements in the pipeline
    audio_pipeline_register(*pipeline, *i2s_writer, "i2s_writer");
    audio_pipeline_register(*pipeline, *aac_decoder, "aac_decoder");
    audio_pipeline_register(*pipeline, *fatfs_reader, "fatfs_reader");
    ESP_LOGI(TAG, "Audio elements registered in pipeline successfully.");
    // Link the audio elements in the pipeline
    const char *link_tag[3] = {"fatfs_reader", "aac_decoder", "i2s_writer"};
    audio_pipeline_link(*pipeline, link_tag, 3);
    return pipeline;
}

void cleanup_audio_playback(){
    if(audio_pipeline_player==NULL){
        return;
    }
    if (!is_playing) {
        ESP_LOGW(TAG, "Playback system is not initialized.");
        return;
    }
    ESP_LOGI(TAG, "Stopping audio playback...");
    xSemaphoreTake(audio_semaphore, portMAX_DELAY); // Wait for semaphore
    // Stop the audio pipeline
    audio_pipeline_stop(*audio_pipeline_player);
    audio_pipeline_wait_for_stop(*audio_pipeline_player);
    ESP_LOGI(TAG, "Audio playback stopped successfully.");
    // Unregister and deinitialize audio elements
    audio_pipeline_unregister(*audio_pipeline_player, *i2s_stream_writer);
    audio_pipeline_unregister(*audio_pipeline_player, *aac_decoder);
    audio_pipeline_unregister(*audio_pipeline_player, *fatfs_stream_reader);

    audio_element_deinit(*i2s_stream_writer);
    audio_element_deinit(*aac_decoder);
    audio_element_deinit(*fatfs_stream_reader);
    free(i2s_stream_writer);
    free(aac_decoder);
    free(fatfs_stream_reader);
    // Deinitialize the audio pipeline
    audio_pipeline_deinit(*audio_pipeline_player);
    free(audio_pipeline_player);
    audio_pipeline_player = NULL;
    i2s_stream_writer = NULL;
    aac_decoder = NULL;
    fatfs_stream_reader = NULL;
    is_playing = false; // Reset playback flag
    ESP_LOGI(TAG, "Audio playback system stopped and cleaned up successfully.");
    xSemaphoreGive(audio_semaphore); // Release semaphore
}

void start_audio_playback(void *pvParameters){
    if (is_playing) {
        ESP_LOGW(TAG, "Playback system is already initialized.");
        return;
    }
    is_playing = true;
    ESP_LOGI(TAG, "Initializing playback system...");
    xSemaphoreTake(audio_semaphore, portMAX_DELAY); // Wait for semaphore
    // setup audio elements
    fatfs_stream_reader = setup_fatfs_stream_reader();
    aac_decoder = setup_aac_decoder();
    i2s_stream_writer = setup_i2s_stream_writer();
    if (!fatfs_stream_reader || !aac_decoder || !i2s_stream_writer) {
        ESP_LOGE(TAG, "Failed to initialize audio elements for playback.");
        cleanup_audio_playback();
        xSemaphoreGive(audio_semaphore); // Release semaphore
        return;
    }
    // setup audio pipeline
    audio_pipeline_player = setup_audio_playback_pipeline(i2s_stream_writer, aac_decoder, fatfs_stream_reader);
    if (!audio_pipeline_player) {
        ESP_LOGE(TAG, "Failed to create audio pipeline for playback.");
        cleanup_audio_playback();
        xSemaphoreGive(audio_semaphore); // Release semaphore
        return;
    }
    char base_dir[20];
    get_base_path(base_dir, 20);
    char input_file[50];
    snprintf(input_file, sizeof(input_file), "%s/recording.aac", base_dir);
    audio_element_set_uri(*fatfs_stream_reader, input_file);
    ESP_LOGI(TAG, "Input file path: %s", input_file);
    // setup event listener
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    if (!evt) {
        ESP_LOGE(TAG, "Failed to create audio event interface.");
        cleanup_audio_playback();
        xSemaphoreGive(audio_semaphore); // Release semaphore
        return;
    }
    ESP_LOGI(TAG, "Audio event interface created successfully.");
    // Register the event listener
    audio_pipeline_set_listener(*audio_pipeline_player, evt);
    audio_pipeline_run(*audio_pipeline_player);
    i2s_stream_set_clk(*i2s_stream_writer, 44100, 16, 1);
    is_playing = true; // Set playback flag
    should_play = true; // Set flag to indicate playback should start
    uint64_t tick_start = esp_timer_get_time();
    ESP_LOGI(TAG, "Audio playback system initialized successfully.");
    // enable speaker
    enable_speaker();
    // i2s_alc_volume_set(*i2s_stream_writer,40);

    while (should_play)
    {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret == ESP_OK) {
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == *aac_decoder) {
                if (msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                   audio_element_info_t info;
                    audio_element_getinfo(*aac_decoder, &info);
                    ESP_LOGI(TAG, "Playback info: Sample rate: %d, Channels: %d, Bitrate: %d",
                             info.sample_rates, info.channels, info.bits);
                             audio_element_setinfo(*i2s_stream_writer, &info);
                             i2s_stream_set_clk(*i2s_stream_writer, info.sample_rates, info.bits, info.channels);
                }
            }
             /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            should_play = false; // Set flag to stop playback
            break;
        }
        }
        ESP_LOGI(TAG, "Playing audio for %lld seconds", (esp_timer_get_time() - tick_start) / 1000000);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
    }
    disable_speaker(); // Disable speaker after playback
    xSemaphoreGive(audio_semaphore); // Release semaphore
    ESP_LOGI(TAG, "Audio playback system started successfully.");
    cleanup_audio_playback(); // Stop the playback system after initialization
    is_playing = false; // Reset playback flag
    vTaskDelete(NULL); // Delete the task after stopping
}
void start_audio_playback_task(){
    xTaskCreate(start_audio_playback, "start_audio_playback", 8096, NULL, 15, NULL);
}
void play_pause_button_press_handler(){
    if (is_playing) {
        ESP_LOGI(TAG, "Pausing audio playback...");
        should_play = false; // Set flag to pause playback
        is_playing = false; // Reset playback flag
    } else{
        start_audio_playback_task(); // Start the playback task
        ESP_LOGI(TAG, "Starting audio playback...");
    }
}