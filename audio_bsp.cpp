#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
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
    if (esp_codec_dev_open(record, &fs) == ESP_CODEC_DEV_OK) {
        Serial.printf("Audio record opened (16000 Hz, stereo, 16-bit)\n");
    }
    if (esp_codec_dev_open(playback, &fs) == ESP_CODEC_DEV_OK) {
        Serial.printf("Audio playback opened (16000 Hz, stereo, 16-bit)\n");
        codec_opened = true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
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

static void build_wav_header_generic(uint8_t* buf, uint32_t data_size, int sr, int ch, int bps)
{
    uint32_t file_size = 36 + data_size;
    uint32_t byte_rate = sr * ch * (bps / 8);
    uint16_t block_align = ch * (bps / 8);

    memcpy(buf, "RIFF", 4);
    buf[4] = file_size & 0xFF;
    buf[5] = (file_size >> 8) & 0xFF;
    buf[6] = (file_size >> 16) & 0xFF;
    buf[7] = (file_size >> 24) & 0xFF;
    memcpy(buf + 8, "WAVE", 4);

    memcpy(buf + 12, "fmt ", 4);
    buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;
    buf[20] = 1; buf[21] = 0;
    buf[22] = ch & 0xFF;
    buf[23] = (ch >> 8) & 0xFF;
    buf[24] = sr & 0xFF;
    buf[25] = (sr >> 8) & 0xFF;
    buf[26] = (sr >> 16) & 0xFF;
    buf[27] = (sr >> 24) & 0xFF;
    buf[28] = byte_rate & 0xFF;
    buf[29] = (byte_rate >> 8) & 0xFF;
    buf[30] = (byte_rate >> 16) & 0xFF;
    buf[31] = (byte_rate >> 24) & 0xFF;
    buf[32] = block_align & 0xFF;
    buf[33] = (block_align >> 8) & 0xFF;
    buf[34] = bps & 0xFF;
    buf[35] = (bps >> 8) & 0xFF;

    memcpy(buf + 36, "data", 4);
    buf[40] = data_size & 0xFF;
    buf[41] = (data_size >> 8) & 0xFF;
    buf[42] = (data_size >> 16) & 0xFF;
    buf[43] = (data_size >> 24) & 0xFF;
}

void audio_beep_play(audio_beep_t type)
{
    int freq = (type == AUDIO_BEEP_START) ? 800 : 500;
    int sample_rate = 16000;
    int channels = 2;
    int bps = 16;
    int silence_ms = 80;
    int tone_ms = 100;
    int silence_samples = sample_rate * silence_ms / 1000;
    int tone_samples = sample_rate * tone_ms / 1000;
    int num_samples = silence_samples + tone_samples;
    int data_size = num_samples * channels * (bps / 8);

    Serial.printf("BEEP-DIRECT: %s  freq=%dHz  sr=%d  data=%d bytes  (tone=%dms silence=%dms)\n",
        type == AUDIO_BEEP_START ? "START" : "STOP",
        freq, sample_rate, data_size, tone_ms, silence_ms);

    int16_t* samples = (int16_t*)malloc(data_size);
    if (!samples) samples = (int16_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    if (!samples) { Serial.printf("BEEP-DIRECT: alloc failed\n"); return; }

    const float amplitude = 6000.0f;
    const float two_pi_f = 2.0f * 3.14159265f * freq;
    int fade_in_samples = sample_rate * 10 / 1000;
    int fade_out_samples = sample_rate * 20 / 1000;

    for (int i = 0; i < num_samples; i++) {
        float value = 0.0f;
        int ti = i - silence_samples;
        if (ti >= 0 && ti < tone_samples) {
            float t = (float)ti / sample_rate;
            value = sinf(two_pi_f * t) * amplitude;

            float envelope = 1.0f;
            if (ti < fade_in_samples) {
                envelope = (float)ti / fade_in_samples;
            } else if (ti >= tone_samples - fade_out_samples) {
                envelope = (float)(tone_samples - 1 - ti) / fade_out_samples;
            }
            value *= envelope;
        }
        int16_t s = (int16_t)value;
        samples[i * 2] = s;
        samples[i * 2 + 1] = s;
    }

    uint8_t* ptr = (uint8_t*)samples;
    int remaining = data_size;
    int writes_ok = 0, writes_fail = 0;
    while (remaining > 0) {
        int chunk = remaining > 1024 ? 1024 : remaining;
        if (esp_codec_dev_write(playback, ptr, chunk) == ESP_CODEC_DEV_OK) {
            ptr += chunk;
            remaining -= chunk;
            writes_ok++;
        } else {
            writes_fail++;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    Serial.printf("BEEP-DIRECT: done  ok=%d  fail=%d\n", writes_ok, writes_fail);

    free(samples);
}

void audio_beep_play_standalone(audio_beep_t type)
{
    int freq = (type == AUDIO_BEEP_START) ? 800 : 500;
    int duration_ms = (type == AUDIO_BEEP_STOP) ? 150 : 200;
    int sample_rate = 24000;
    int channels = 1;
    int bps = 16;
    int silence_ms = 70;
    int tone_ms = duration_ms - silence_ms;
    int tone_samples = sample_rate * tone_ms / 1000;
    int silence_samples = sample_rate * silence_ms / 1000;
    int num_samples = tone_samples + silence_samples;
    int data_size = num_samples * channels * (bps / 8);
    size_t total_size = 44 + data_size;

    Serial.printf("BEEP-WAV: %s  freq=%dHz  sr=%d  ch=%d  data=%d bytes  total=%u  (tone=%dms silence=%dms)\n",
        type == AUDIO_BEEP_START ? "START" : "STOP",
        freq, sample_rate, channels, data_size, total_size, tone_ms, silence_ms);

    uint8_t* buf = (uint8_t*)malloc(total_size);
    if (!buf) buf = (uint8_t*)heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
    if (!buf) { Serial.printf("BEEP-WAV: alloc failed\n"); return; }

    build_wav_header_generic(buf, data_size, sample_rate, channels, bps);

    int16_t* samples = (int16_t*)(buf + 44);
    const float amplitude = 8000.0f;
    const float two_pi_f = 2.0f * 3.14159265f * freq;
    int fade_in_samples = sample_rate * 10 / 1000;
    int fade_out_samples = sample_rate * 30 / 1000;

    for (int i = 0; i < num_samples; i++) {
        float value = 0.0f;
        int ti = i - silence_samples;
        if (ti >= 0 && ti < tone_samples) {
            float t = (float)ti / sample_rate;
            value = sinf(two_pi_f * t) * amplitude;

            float envelope = 1.0f;
            if (ti < fade_in_samples) {
                envelope = (float)ti / fade_in_samples;
            } else if (ti >= tone_samples - fade_out_samples) {
                envelope = (float)(tone_samples - 1 - ti) / fade_out_samples;
            }
            value *= envelope;
        }
        samples[i] = (int16_t)value;
    }

    audio_play_wav_start(buf, total_size);
    while (audio_wav_is_playing()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(buf);
    Serial.printf("BEEP-WAV: done\n");
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
            Serial.printf("FATAL: PSRAM insufficient, restarting...\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP.restart();
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

uint32_t audio_stop_recording_no_close(void)
{
    recording_active = false;
    if (rec_task_handle) {
        while (rec_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint32_t bytes = rec_bytes_written;
    Serial.printf("Recording stopped (no close): %u bytes data\n", bytes);
    return bytes;
}

void audio_close_codec(void)
{
    if (playback) {
        esp_codec_dev_close(playback);
    }
    if (record) {
        esp_codec_dev_close(record);
    }
    codec_opened = false;
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

void audio_free_recording_buffer(void)
{
    if (rec_buffer) {
        free(rec_buffer);
        rec_buffer = NULL;
        Serial.printf("Recording buffer freed, heap=%u\n", esp_get_free_heap_size());
    }
    rec_bytes_written = 0;
}

uint8_t* audio_get_wav_buffer(void)
{
    return rec_buffer;
}

uint32_t audio_get_wav_size(void)
{
    return 44 + rec_bytes_written;
}

/* ---------- WAV playback ---------- */

static uint8_t* wav_buffer = NULL;
static size_t wav_size = 0;
static volatile bool wav_playing = false;
static volatile bool wav_stop_flag = false;
static volatile bool wav_replay_flag = false;
static TaskHandle_t wav_task_handle = NULL;
static bool wav_pcm_override = false;
static int wav_pcm_sr = 0;
static int wav_pcm_ch = 0;
static int wav_pcm_bps = 0;

static bool parse_wav_header(uint8_t* buf, size_t size, int* sr, int* ch, int* bps, uint32_t* data_offset, uint32_t* data_size)
{
    if (size < 44) return false;
    if (memcmp(buf, "RIFF", 4) != 0) return false;
    if (memcmp(buf + 8, "WAVE", 4) != 0) return false;
    if (memcmp(buf + 12, "fmt ", 4) != 0) return false;

    int fmt_chunk_size = buf[16] | (buf[17] << 8) | (buf[18] << 16) | (buf[19] << 24);
    int audio_format = buf[20] | (buf[21] << 8);
    if (audio_format != 1) return false;

    *ch = buf[22] | (buf[23] << 8);
    if (*ch < 1 || *ch > 2) *ch = 2;
    *sr = buf[24] | (buf[25] << 8) | (buf[26] << 16) | (buf[27] << 24);
    *bps = buf[34] | (buf[35] << 8);

    size_t offset = 20 + fmt_chunk_size;
    while (offset + 8 <= size) {
        if (memcmp(buf + offset, "data", 4) == 0) {
            *data_offset = offset + 8;
            *data_size = buf[offset + 4] | (buf[offset + 5] << 8) | (buf[offset + 6] << 16) | (buf[offset + 7] << 24);
            if (*data_offset + *data_size > size) {
                *data_size = size - *data_offset;
            }
            return true;
        }
        uint32_t chunk_size = buf[offset + 4] | (buf[offset + 5] << 8) | (buf[offset + 6] << 16) | (buf[offset + 7] << 24);
        offset += 8 + chunk_size;
    }

    *data_offset = 44;
    *data_size = size - 44;
    return true;
}

static void wav_playback_task(void *arg)
{
    if (codec_opened) {
        esp_codec_dev_close(playback);
        esp_codec_dev_close(record);
        codec_opened = false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    int sr, ch, bps;
    uint32_t data_offset, data_size;
    if (wav_pcm_override) {
        sr = wav_pcm_sr;
        ch = wav_pcm_ch;
        bps = wav_pcm_bps;
        data_offset = 0;
        data_size = (uint32_t)wav_size;
        wav_pcm_override = false;
    } else if (!parse_wav_header(wav_buffer, wav_size, &sr, &ch, &bps, &data_offset, &data_size)) {
        Serial.printf("WAV: invalid header\n");
        wav_playing = false;
        wav_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    Serial.printf("WAV playback: %d Hz, %d ch, %d bps, %u bytes PCM\n", sr, ch, bps, data_size);

    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = sr;
    fs.channel = ch;
    fs.bits_per_sample = bps;
    esp_codec_dev_set_out_vol(playback, 100.0);
    esp_codec_dev_open(playback, &fs);
    codec_opened = true;
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t* pcm = wav_buffer + data_offset;
    uint32_t remaining = data_size;
    uint8_t* ptr = pcm;
    uint32_t pos = 0;

    while (remaining > 0 && !wav_stop_flag) {
        if (wav_replay_flag) {
            wav_replay_flag = false;
            ptr = pcm;
            pos = 0;
            remaining = data_size;
        }
        uint32_t chunk = remaining > 1024 ? 1024 : remaining;
        int ret = esp_codec_dev_write(playback, ptr + pos, chunk);
        if (ret == ESP_CODEC_DEV_OK) {
            pos += chunk;
            remaining -= chunk;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    esp_codec_dev_close(playback);
    codec_opened = false;
    wav_playing = false;
    wav_task_handle = NULL;
    activity_feed();
    Serial.printf("WAV playback finished\n");
    vTaskDelete(NULL);
}

void audio_play_wav_start(uint8_t* buf, size_t size)
{
    if (wav_playing) {
        wav_stop_flag = true;
        while (wav_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    }
    wav_buffer = buf;
    wav_size = size;
    wav_pcm_override = false;
    wav_stop_flag = false;
    wav_replay_flag = false;
    wav_playing = true;
    xTaskCreatePinnedToCore(wav_playback_task, "wav_task", 8 * 1024, NULL, 5, &wav_task_handle, 1);
}

void audio_play_pcm_start(uint8_t* pcm_data, size_t pcm_size, int sample_rate, int channels, int bits)
{
    if (wav_playing) {
        wav_stop_flag = true;
        while (wav_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    }
    wav_buffer = pcm_data;
    wav_size = pcm_size;
    wav_pcm_override = true;
    wav_pcm_sr = sample_rate;
    wav_pcm_ch = channels;
    wav_pcm_bps = bits;
    wav_stop_flag = false;
    wav_replay_flag = false;
    wav_playing = true;
    xTaskCreatePinnedToCore(wav_playback_task, "wav_task", 8 * 1024, NULL, 5, &wav_task_handle, 1);
}

void audio_play_wav_stop(void)
{
    wav_stop_flag = true;
    while (wav_task_handle) vTaskDelay(pdMS_TO_TICKS(10));
    wav_playing = false;
}

void audio_play_wav_replay(void)
{
    if (wav_playing) wav_replay_flag = true;
}

bool audio_wav_is_playing(void)
{
    return wav_playing;
}
