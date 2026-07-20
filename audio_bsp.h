#ifndef AUDIO_BSP_H
#define AUDIO_BSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_bsp_init(void);
void audio_play_init(void);
void audio_playback_set_vol(uint8_t vol);
void audio_playback_read(void *data_ptr, uint32_t len);
void audio_playback_write(void *data_ptr, uint32_t len);

void audio_start_recording(void);
uint32_t audio_stop_recording(void);
void audio_discard_recording(void);
void audio_free_recording_buffer(void);
uint8_t* audio_get_wav_buffer(void);
uint32_t audio_get_wav_size(void);

void audio_play_wav_start(uint8_t* wav_buffer, size_t wav_size);
void audio_play_wav_stop(void);
void audio_play_wav_replay(void);
bool audio_wav_is_playing(void);
void audio_play_pcm_start(uint8_t* pcm_data, size_t pcm_size, int sample_rate, int channels, int bits);

typedef enum {
    AUDIO_BEEP_START = 0,
    AUDIO_BEEP_STOP = 1,
    AUDIO_BEEP_DISCARD = 2,
    AUDIO_BEEP_RECONNECT = 3,
    AUDIO_BEEP_SLEEP = 4,
    AUDIO_BEEP_WAKE = 5,
} audio_beep_t;

void audio_beep_play(audio_beep_t type);
void audio_beep_play_standalone(audio_beep_t type);
uint32_t audio_stop_recording_no_close(void);
void audio_close_codec(void);

#ifdef __cplusplus
}
#endif

#endif
