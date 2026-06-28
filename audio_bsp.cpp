#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "audio_bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "src/codec_board/codec_board.h"
#include "src/codec_board/codec_init.h"
#include "src/esp_codec_dev/include/esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "user_app.h"

extern QueueHandle_t state_queue;

static esp_codec_dev_handle_t playback = NULL;
static esp_codec_dev_handle_t record = NULL;
static bool codec_opened = false;

#define REC_SAMPLE_RATE   16000
#define REC_CHANNELS       2
#define REC_BITS_PER_SAMPLE 16
#define REC_BYTES_PER_SAMPLE (REC_BITS_PER_SAMPLE / 8)
#define REC_MAX_SECONDS    30
#define REC_CHUNK_SIZE     1024
#define REC_BUFFER_SIZE    (REC_SAMPLE_RATE * REC_CHANNELS * REC_BYTES_PER_SAMPLE * REC_MAX_SECONDS)

static uint8_t* rec_buffer = NULL;
static uint32_t rec_bytes_written = 0;
static volatile bool recording_active = false;
static TaskHandle_t rec_task_handle = NULL;

void audio_bsp_init(void)
{
    set_codec_board_type("S3_ePaper_1_54");
    codec_init_cfg_t codec_cfg = {
        .in_mode = CODEC_I2S_MODE_STD,
        .out_mode = CODEC_I2S_MODE_STD,
        .in_use_tdm = false,
        .reuse_dev = false,
    };
    ESP_ERROR_CHECK(init_codec(&codec_cfg));
    playback = get_playback_handle();
    record = get_record_handle();
    Serial.printf("Audio codec initialized: ES8311 OK\n");
}

void audio_play_init(void)
{
    if (codec_opened) {
        esp_codec_dev_close(record);
        esp_codec_dev_close(playback);
        codec_opened = false;
    }
    esp_codec_dev_set_out_vol(playback, 100.0);
    esp_codec_dev_set_in_gain(record, 45.0);
    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = 16000;
    fs.channel = 2;
    fs.bits_per_sample = 16;
    if (esp_codec_dev_open(playback, &fs) == ESP_CODEC_DEV_OK) {
        Serial.printf("Audio playback opened (16000 Hz, stereo, 16-bit)\n");
        codec_opened = true;
    }
    if (esp_codec_dev_open(record, &fs) == ESP_CODEC_DEV_OK) {
        Serial.printf("Audio record opened (16000 Hz, stereo, 16-bit)\n");
    }
}

void audio_playback_set_vol(uint8_t vol)
{
    esp_codec_dev_set_out_vol(playback, vol);
}

void audio_playback_read(void *data_ptr, uint32_t len)
{
    esp_codec_dev_read(record, data_ptr, len);
}

void audio_playback_write(void *data_ptr, uint32_t len)
{
    esp_codec_dev_write(playback, data_ptr, len);
}

static void build_wav_header(uint8_t* buf, uint32_t data_size)
{
    uint32_t file_size = 36 + data_size;
    uint32_t byte_rate = REC_SAMPLE_RATE * REC_CHANNELS * REC_BYTES_PER_SAMPLE;
    uint16_t block_align = REC_CHANNELS * REC_BYTES_PER_SAMPLE;

    memcpy(buf, "RIFF", 4);
    buf[4] = file_size & 0xFF;
    buf[5] = (file_size >> 8) & 0xFF;
    buf[6] = (file_size >> 16) & 0xFF;
    buf[7] = (file_size >> 24) & 0xFF;
    memcpy(buf + 8, "WAVE", 4);

    memcpy(buf + 12, "fmt ", 4);
    buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;
    buf[20] = 1; buf[21] = 0;
    buf[22] = REC_CHANNELS & 0xFF;
    buf[23] = (REC_CHANNELS >> 8) & 0xFF;
    buf[24] = REC_SAMPLE_RATE & 0xFF;
    buf[25] = (REC_SAMPLE_RATE >> 8) & 0xFF;
    buf[26] = (REC_SAMPLE_RATE >> 16) & 0xFF;
    buf[27] = (REC_SAMPLE_RATE >> 24) & 0xFF;
    buf[28] = byte_rate & 0xFF;
    buf[29] = (byte_rate >> 8) & 0xFF;
    buf[30] = (byte_rate >> 16) & 0xFF;
    buf[31] = (byte_rate >> 24) & 0xFF;
    buf[32] = block_align & 0xFF;
    buf[33] = (block_align >> 8) & 0xFF;
    buf[34] = REC_BITS_PER_SAMPLE & 0xFF;
    buf[35] = (REC_BITS_PER_SAMPLE >> 8) & 0xFF;

    memcpy(buf + 36, "data", 4);
    buf[40] = data_size & 0xFF;
    buf[41] = (data_size >> 8) & 0xFF;
    buf[42] = (data_size >> 16) & 0xFF;
    buf[43] = (data_size >> 24) & 0xFF;
}

static void recording_task(void *arg)
{
    uint8_t* buf = rec_buffer + 44;
    uint32_t max_data = REC_BUFFER_SIZE - 44;
    rec_bytes_written = 0;

    while (recording_active && rec_bytes_written < max_data) {
        uint32_t to_read = REC_CHUNK_SIZE;
        if (rec_bytes_written + to_read > max_data) {
            to_read = max_data - rec_bytes_written;
        }
        esp_err_t ret = esp_codec_dev_read(record, buf + rec_bytes_written, to_read);
        if (ret == ESP_CODEC_DEV_OK) {
            rec_bytes_written += to_read;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    recording_active = false;
    build_wav_header(rec_buffer, rec_bytes_written);
    Serial.printf("Recording done: %u bytes captured\n", rec_bytes_written);
    AppEvent evt = { EVT_RECORDING_DONE, 0 };
    xQueueSend(state_queue, &evt, 0);
    rec_task_handle = NULL;
    vTaskDelete(NULL);
}

void audio_start_recording(void)
{
    if (recording_active) return;

    if (!rec_buffer) {
        rec_buffer = (uint8_t*)heap_caps_malloc(REC_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (!rec_buffer) {
            Serial.printf("ERROR: Cannot allocate %u bytes in PSRAM for recording\n", REC_BUFFER_SIZE);
            return;
        }
    }

    rec_bytes_written = 0;
    recording_active = true;
    xTaskCreatePinnedToCore(recording_task, "rec_task", 8 * 1024, NULL, 5, &rec_task_handle, 1);
    Serial.printf("Recording started...\n");
}

uint32_t audio_stop_recording(void)
{
    recording_active = false;
    uint32_t bytes = 0;
    if (rec_task_handle) {
        while (rec_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    }
    bytes = rec_bytes_written;
    esp_codec_dev_close(record);
    esp_codec_dev_close(playback);
    codec_opened = false;
    Serial.printf("Recording stopped: %u bytes data\n", bytes);
    return bytes;
}

void audio_discard_recording(void)
{
    recording_active = false;
    if (rec_task_handle) {
        while (rec_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_codec_dev_close(record);
    esp_codec_dev_close(playback);
    codec_opened = false;
    if (rec_buffer) {
        free(rec_buffer);
        rec_buffer = NULL;
    }
    rec_bytes_written = 0;
    Serial.printf("Recording discarded.\n");
}

uint8_t* audio_get_wav_buffer(void)
{
    return rec_buffer;
}

uint32_t audio_get_wav_size(void)
{
    return 44 + rec_bytes_written;
}

/* ---------- MP3 playback ---------- */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static uint8_t* mp3_buffer = NULL;
static size_t mp3_size = 0;
static volatile bool mp3_playing = false;
static volatile bool mp3_stop_flag = false;
static volatile bool mp3_replay_flag = false;
static TaskHandle_t mp3_task_handle = NULL;

static void mp3_playback_task(void *arg)
{
    esp_codec_dev_close(playback);
    esp_codec_dev_close(record);
    codec_opened = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    mp3d_sample_t* pcm = (mp3d_sample_t*)malloc(sizeof(mp3d_sample_t) * MINIMP3_MAX_SAMPLES_PER_FRAME);
    if (!pcm) { mp3_playing = false; mp3_task_handle = NULL; vTaskDelete(NULL); return; }

    int sr = 44100;
    int ch = 2;
    {
        mp3dec_t d;
        mp3dec_frame_info_t inf;
        mp3dec_init(&d);
        if (mp3dec_decode_frame(&d, mp3_buffer, (int)mp3_size, pcm, &inf) > 0 && inf.hz > 0) {
            sr = inf.hz;
            ch = inf.channels > 0 ? inf.channels : 2;
        }
    }

    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = sr;
    fs.channel = ch;
    fs.bits_per_sample = 16;
    esp_codec_dev_set_out_vol(playback, 100.0);
    esp_codec_dev_open(playback, &fs);
    codec_opened = true;
    Serial.printf("MP3 playback: %d Hz, %d ch\n", sr, ch);
    vTaskDelay(pdMS_TO_TICKS(10));

    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    uint8_t* ptr = mp3_buffer;
    int remaining = (int)mp3_size;

    while (remaining > 0 && !mp3_stop_flag) {
        if (mp3_replay_flag) {
            mp3_replay_flag = false;
            mp3dec_init(&dec);
            ptr = mp3_buffer;
            remaining = (int)mp3_size;
        }
        int samples = mp3dec_decode_frame(&dec, ptr, remaining, pcm, &info);
        if (samples <= 0) break;
        ptr += info.frame_bytes;
        remaining -= info.frame_bytes;
        esp_codec_dev_write(playback, pcm, samples * info.channels * 2);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    esp_codec_dev_close(playback);
    codec_opened = false;
    mp3_playing = false;
    mp3_task_handle = NULL;
    free(pcm);
    Serial.printf("MP3 playback finished\n");
    vTaskDelete(NULL);
}

void audio_play_mp3_start(uint8_t* buf, size_t size)
{
    if (mp3_playing) {
        mp3_stop_flag = true;
        while (mp3_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    }
    mp3_buffer = buf;
    mp3_size = size;
    mp3_stop_flag = false;
    mp3_replay_flag = false;
    mp3_playing = true;
    xTaskCreatePinnedToCore(mp3_playback_task, "mp3_task", 40 * 1024, NULL, 5, &mp3_task_handle, 1);
}

void audio_play_mp3_stop(void)
{
    mp3_stop_flag = true;
    while (mp3_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    mp3_playing = false;
}

void audio_play_mp3_replay(void)
{
    if (mp3_playing) mp3_replay_flag = true;
}

bool audio_mp3_is_playing(void)
{
    return mp3_playing;
}
